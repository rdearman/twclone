import { Component, HostListener } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterOutlet } from '@angular/router';
import { TransportService } from './services/transport.service';
import { StateService } from './services/state.service';
import { MenuService, MenuOption } from './services/menu.service';
import { SectorHudComponent } from './components/sector-hud/sector-hud.component';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterOutlet, SectorHudComponent],
  templateUrl: './app.component.html',
  styleUrl: './app.component.scss'
})
export class AppComponent {
  title = 'TW Client Web';
  
  host = '127.0.0.1';
  port = 1234;
  connected = false;
  
  // Inspector
  rpcCommand = 'system.hello';
  rpcData = '{}';
  rpcResult: any = null;
  rpcError: any = null;
  
  events: any[] = [];
  activeMenu: any = null;

  // Prompt Modal
  pendingPrompt: { label: string, resolve: (val: string) => void } | null = null;
  promptValue = '';

  constructor(
    private transport: TransportService,
    public state: StateService,
    public menuService: MenuService
  ) {
    this.transport.connected$.subscribe(c => this.connected = c);
    this.transport.events$.subscribe(e => {
      this.events.unshift(e);
      if (this.events.length > 50) this.events.pop();
    });
    this.menuService.activeMenu$.subscribe(m => this.activeMenu = m);
    
    this.state.prompt$.subscribe(p => {
      this.pendingPrompt = p;
      this.promptValue = '';
    });
  }

  @HostListener('window:keydown', ['$event'])
  handleKeyDown(event: KeyboardEvent) {
    if (this.pendingPrompt) {
        if (event.key === 'Enter') {
            this.submitPrompt();
            return;
        }
        if (event.key === 'Escape') {
            this.pendingPrompt.resolve('');
            this.pendingPrompt = null;
            return;
        }
        return;
    }

    if (['INPUT', 'TEXTAREA'].includes((event.target as HTMLElement).tagName)) {
      return;
    }

    if (this.activeMenu) {
      const key = event.key.toUpperCase();
      const option = this.activeMenu.options.find((o: MenuOption) => o.key.toUpperCase() === key);
      if (option) {
        this.menuService.selectOption(option);
      } else if (event.key === 'Escape') {
        this.menuService.navigateBack();
      }
    }
  }

  submitPrompt() {
    if (this.pendingPrompt) {
        this.pendingPrompt.resolve(this.promptValue);
        this.pendingPrompt = null;
    }
  }

  connect() {
    this.transport.connectToGameServer(this.host, this.port);
  }

  sendRpc() {
    let data = {};
    try {
      data = JSON.parse(this.rpcData);
    } catch (e) {
      alert('Invalid JSON data');
      return;
    }
    
    this.rpcResult = null;
    this.rpcError = null;
    this.state.lastRpcRequest$.next({ command: this.rpcCommand, data });

    this.transport.sendRpc(this.rpcCommand, data).subscribe({
      next: (res) => {
        this.rpcResult = res;
        this.state.lastRpcResponse$.next(res);
        this.state.updateFromRpc(res);
      },
      error: (err) => {
        this.rpcError = err;
      }
    });
  }

  selectOption(opt: MenuOption) {
    this.menuService.selectOption(opt);
  }
}
