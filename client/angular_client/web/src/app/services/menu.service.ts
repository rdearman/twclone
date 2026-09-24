import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { BehaviorSubject, firstValueFrom } from 'rxjs';
import { TransportService } from './transport.service';
import { StateService } from './state.service';
import { ResolverService } from './resolver.service';

export interface MenuOption {
  key: string;
  label: string;
  action: {
    submenu?: string;
    back?: boolean;
    rpc?: {
      command: string;
      data: any;
    };
    flow?: string;
  };
  show_if?: string;
  show_if_ctx?: string[];
}

export interface MenuDef {
  title: string;
  options: MenuOption[];
  on_enter?: any;
}

@Injectable({
  providedIn: 'root'
})
export class MenuService {
  private menus: Record<string, MenuDef> = {};
  private menuStack: string[] = ['MAIN'];
  
  activeMenu$ = new BehaviorSubject<MenuDef | null>(null);
  menuStack$ = new BehaviorSubject<string[]>(['MAIN']);

  constructor(
    private http: HttpClient,
    private transport: TransportService,
    private state: StateService,
    private resolver: ResolverService
  ) {
    this.loadMenus();
  }

  async loadMenus() {
    try {
      const data = await firstValueFrom(this.http.get<Record<string, MenuDef>>('assets/menus.json'));
      this.menus = data;
      this.updateActiveMenu();
    } catch (e) {
      console.error('Failed to load menus', e);
    }
  }

  private updateActiveMenu() {
    const currentId = this.menuStack[this.menuStack.length - 1];
    const menu = this.menus[currentId];
    if (menu) {
      this.activeMenu$.next(menu);
      this.menuStack$.next([...this.menuStack]);
    }
  }

  async selectOption(option: MenuOption) {
    const action = option.action;
    
    if (action.submenu) {
      this.menuStack.push(action.submenu);
      this.updateActiveMenu();
    } else if (action.back) {
      if (this.menuStack.length > 1) {
        this.menuStack.pop();
        this.updateActiveMenu();
      }
    } else if (action.rpc) {
      const resolvedData = await this.resolver.resolve(action.rpc.data);
      this.transport.sendRpc(action.rpc.command, resolvedData).subscribe(res => {
        this.state.lastRpcResponse$.next(res);
        this.state.updateFromRpc(res);
      });
    }
  }

  navigateBack() {
    if (this.menuStack.length > 1) {
      this.menuStack.pop();
      this.updateActiveMenu();
    }
  }
}
