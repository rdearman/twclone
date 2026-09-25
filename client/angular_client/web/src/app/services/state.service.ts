import { Injectable } from '@angular/core';
import { BehaviorSubject, Subject } from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class StateService {
  lastRpcRequest$ = new BehaviorSubject<any>(null);
  lastRpcResponse$ = new BehaviorSubject<any>(null);
  
  // Game State
  gameState: any = {
    player: {},
    sector: {},
    state: {}
  };
  
  // Modal Prompt
  prompt$ = new Subject<{ label: string, resolve: (val: string) => void }>();

  constructor() { }

  updateFromRpc(res: any) {
    if (!res || !res.ok) return;
    const data = res.result?.data;
    if (!data) return;

    // Basic heuristic to update state
    if (data.player) this.gameState.player = { ...this.gameState.player, ...data.player };
    if (data.sector_id || data.adjacent_sectors) {
        this.gameState.sector = { ...this.gameState.sector, ...data };
    }
    if (data.ships) this.gameState.sector.ships = data.ships;
  }
}
