import { Injectable } from '@angular/core';
import { webSocket, WebSocketSubject } from 'rxjs/webSocket';
import { Observable, Subject, filter, map, take, tap } from 'rxjs';

export interface RpcMessage {
  type: 'rpc';
  id: string;
  command: string;
  data: any;
  auth?: any;
}

export interface RpcResult {
  type: 'rpc_result';
  id: string;
  ok: boolean;
  result?: any;
  error?: any;
}

export interface EventMessage {
  type: 'event';
  event: any;
}

@Injectable({
  providedIn: 'root'
})
export class TransportService {
  private socket$: WebSocketSubject<any> | null = null;
  public events$ = new Subject<any>();
  public connected$ = new Subject<boolean>();

  constructor() { }

  connectToGateway() {
    if (this.socket$) return;

    this.socket$ = webSocket('ws://localhost:8081/ws');
    
    this.socket$.subscribe({
      next: (msg) => this.handleMessage(msg),
      error: (err) => {
        console.error('WebSocket error:', err);
        this.connected$.next(false);
        this.socket$ = null;
      },
      complete: () => {
        console.log('WebSocket closed');
        this.connected$.next(false);
        this.socket$ = null;
      }
    });
  }

  connectToGameServer(host: string, port: number) {
    if (!this.socket$) this.connectToGateway();
    
    this.socket$?.next({
      type: 'connect',
      host,
      port
    });
  }

  sendRpc(command: string, data: any = {}): Observable<RpcResult> {
    if (!this.socket$) throw new Error('Not connected');

    const id = crypto.randomUUID();
    const payload: RpcMessage = {
      type: 'rpc',
      id,
      command,
      data
    };

    this.socket$.next(payload);

    return this.socket$.pipe(
      filter((msg: any) => msg.type === 'rpc_result' && msg.id === id),
      take(1),
      map(msg => msg as RpcResult)
    );
  }

  private handleMessage(msg: any) {
    console.log('RX:', msg);
    if (msg.type === 'connected') {
      this.connected$.next(true);
    } else if (msg.type === 'event') {
      this.events$.next(msg.event);
    }
    // rpc_result is handled by the subscription in sendRpc
  }
}