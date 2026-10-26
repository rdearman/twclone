import { Component, OnDestroy, OnInit, Optional } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { HttpErrorResponse } from '@angular/common/http';
import { ClientEventTransport, GameCommandTransport, GatewayPlayerSummary, GatewayTransport } from './services/transport.service';
import { StateService } from './services/state.service';
import { BrowserEventCursorStore, EventBridgeCoordinator } from './services/event-bridge.service';
import { firstValueFrom } from 'rxjs';
import { Subscription } from 'rxjs';
import { ClientLifecycleService } from './services/client-lifecycle.service';
import { Capacitor } from '@capacitor/core';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './app.component.html',
  styleUrl: './app.component.scss'
})
export class AppComponent implements OnInit, OnDestroy {
  readonly title = 'TWClone';
  username = '';
  password = '';
  busy = false;
  errorMessage = '';
  playerSummary: GatewayPlayerSummary | null = null;
  playerDetails: unknown = null;
  shipDetails: unknown = null;
  sectorDetails: unknown = null;
  portDetails: unknown = null;
  notices: unknown[] = [];
  commandError = '';
  commandResult: unknown = null;
  destinationSector = '';
  commodity = 'ORE';
  tradeQuantity = 1;
  account = 0;
  targetShipId = '';
  combatDetails: unknown = null;
  readonly isNative = Capacitor.isNativePlatform();
  gatewayOrigin = '';
  private readonly eventBridge: EventBridgeCoordinator | null;
  private readonly eventTransport: ClientEventTransport | null;
  private readonly subscriptions = new Subscription();

  constructor(
    private readonly transport: GatewayTransport,
    private readonly state: StateService,
    @Optional() eventTransport: ClientEventTransport | null,
    @Optional() private readonly commands: GameCommandTransport | null,
    private readonly lifecycle: ClientLifecycleService
  ) {
    this.eventTransport = eventTransport;
    this.gatewayOrigin = transport.gatewayOrigin();
    eventTransport?.configureGatewayOrigin?.(this.gatewayOrigin);
    this.eventBridge = eventTransport
      ? new EventBridgeCoordinator(state, eventTransport, null, new BrowserEventCursorStore(), () => this.refreshPlayerState())
      : null;
    if (eventTransport) {
      this.subscriptions.add(eventTransport.connectionState$.subscribe(connection => {
        if (connection === 'online' && this.playerDetails) this.refreshGameplayState();
      }));
    }
  }

  ngOnDestroy(): void {
    this.lifecycle.stop();
    this.subscriptions.unsubscribe();
    this.eventBridge?.destroy();
  }

  ngOnInit(): void {
    this.lifecycle.start(() => this.eventBridge?.resume(), () => this.eventBridge?.pause());
    this.loadExistingSession();
  }

  get clientState() { return this.state.snapshot(); }
  get sectorShips(): Array<Record<string, unknown>> {
    const ships = this.clientState.sector.value?.['ships_present'];
    if (!Array.isArray(ships)) return [];
    const ownShip = this.shipDetails && typeof this.shipDetails === 'object' && !Array.isArray(this.shipDetails)
      ? this.shipDetails as Record<string, unknown>
      : {};
    const ownShipId = Number(ownShip['ship_id'] ?? ownShip['id']);
    return ships.filter((ship): ship is Record<string, unknown> =>
      !!ship && typeof ship === 'object' && !Array.isArray(ship)
      && Number(ship['ship_id'] ?? ship['id']) !== ownShipId
    );
  }
  get adjacentSectors(): Array<{ id: number; name: string }> {
    const raw = this.clientState.sector.value?.['adjacent_sectors'];
    if (!Array.isArray(raw)) return [];
    return raw.flatMap(value => {
      const item = typeof value === 'number' ? { sector_id: value } : value;
      if (!item || typeof item !== 'object' || Array.isArray(item)) return [];
      const record = item as Record<string, unknown>;
      const id = Number(record['sector_id'] ?? record['id']);
      return Number.isInteger(id) && id > 0 ? [{ id, name: String(record['name'] ?? '') }] : [];
    });
  }

  refreshGameplayState(): void {
    this.runCommand('ship.status', {}, value => this.shipDetails = value['ship'] ?? value);
    this.runCommand('sector.info', {}, value => {
      this.sectorDetails = value;
      this.destinationSector = String(value['sector_id'] ?? this.playerSummary?.current_sector ?? '');
    });
    this.runCommand('notice.list', { limit: 20 }, value => {
      this.notices = Array.isArray(value['items']) ? value['items'] : [];
    });
    this.runCommand('combat.status', {}, value => this.combatDetails = value);
  }

  selectPort(port: Record<string, unknown>): void {
    const portId = Number(port['port_id'] ?? port['id']);
    const sectorId = Number(this.clientState.sector.value?.['sector_id']);
    if (!Number.isInteger(portId) || !Number.isInteger(sectorId)) return;
    this.state.selectPort(portId);
    this.runCommand('port.status', { port_id: portId, sector_id: sectorId }, value => {
      this.portDetails = value['port'] ?? value;
      this.state.acceptPort((value['port'] ?? value) as Record<string, unknown>);
    });
  }

  quoteTrade(): void {
    const portId = this.selectedPortId();
    const quantity = this.validTradeQuantity();
    if (portId === null || quantity === null) return;
    this.runCommand('trade.quote', { port_id: portId, commodity: this.commodity.trim().toUpperCase(), quantity }, value => this.commandResult = value);
  }

  trade(action: 'trade.buy' | 'trade.sell'): void {
    const portId = this.selectedPortId();
    const sectorId = Number(this.clientState.sector.value?.['sector_id']);
    const quantity = this.validTradeQuantity();
    if (portId === null || !Number.isInteger(sectorId) || quantity === null) return;
    const idempotencyKey = globalThis.crypto?.randomUUID?.() ?? `web-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    this.runCommand(action, {
      port_id: portId,
      sector_id: sectorId,
      items: [{ commodity: this.commodity.trim().toUpperCase(), quantity }],
      account: this.account,
      idempotency_key: idempotencyKey
    }, value => {
      this.commandResult = value;
      this.refreshGameplayState();
    });
  }

  warp(): void {
    const destination = Number(this.destinationSector);
    if (!Number.isInteger(destination) || destination < 1) return;
    this.runCommand('move.warp', { to_sector_id: destination }, value => {
      this.commandResult = value;
      this.refreshGameplayState();
    });
  }

  warpTo(sectorId: number): void {
    this.destinationSector = String(sectorId);
    this.warp();
  }

  selectCombatTarget(ship: Record<string, unknown>): void {
    const id = Number(ship['ship_id'] ?? ship['id']);
    if (Number.isInteger(id) && id > 0) this.targetShipId = String(id);
  }

  attackTarget(): void {
    const targetId = Number(this.targetShipId);
    if (!Number.isInteger(targetId) || targetId < 1) {
      this.commandError = 'Choose a ship in this sector first.';
      return;
    }
    this.runCommand('combat.attack', { target_ship_id: targetId }, value => {
      this.combatDetails = value;
      this.commandResult = value;
      this.refreshGameplayState();
    });
  }

  runCombatStatus(): void {
    this.runCommand('combat.status', {}, value => this.combatDetails = value);
  }

  login(): void {
    if (this.busy || !this.username.trim() || !this.password) return;
    if (this.isNative && !this.gatewayOrigin.trim()) {
      this.errorMessage = 'Set the HTTPS gateway address for this Android app first.';
      return;
    }
    this.busy = true;
    this.errorMessage = '';
    this.transport.login(this.username.trim(), this.password).subscribe({
      next: response => {
        this.playerSummary = response.player;
        this.state.markAuthenticated();
        this.state.acceptPlayer(response.player as unknown as Record<string, unknown>);
        this.password = '';
        this.loadPlayerDetails();
      },
      error: error => this.showError(error),
      complete: () => this.busy = false
    });
  }

  saveGatewayOrigin(): void {
    try {
      this.transport.configureGatewayOrigin(this.gatewayOrigin);
      this.gatewayOrigin = this.transport.gatewayOrigin();
      this.configureEventGatewayOrigin(this.gatewayOrigin);
      this.eventBridge?.reconnect();
      this.errorMessage = '';
    } catch (error) {
      this.errorMessage = error instanceof Error ? error.message : 'Gateway URL is invalid.';
    }
  }

  logout(): void {
    if (this.busy) return;
    this.busy = true;
    this.errorMessage = '';
    this.transport.logout().subscribe({
      next: () => this.clearSession(),
      error: error => this.showError(error),
      complete: () => {
        this.clearSession();
        this.state.markUnauthenticated();
        this.busy = false;
      }
    });
  }

  private loadExistingSession(): void {
    this.transport.playerMe().subscribe({
      next: result => {
        this.playerDetails = result.player;
        this.playerSummary = this.summaryFrom(result.player);
        this.state.markAuthenticated();
        const player = this.playerRecord(result.player);
        if (player) this.state.acceptPlayer(player);
        this.startAuthenticatedStream();
      },
      error: error => {
        // Only an authoritative 401 proves there is no usable gateway session.
        if (error instanceof HttpErrorResponse && error.status === 401) {
          this.state.markUnauthenticated();
        }
      }
    });
  }

  private loadPlayerDetails(): void {
    this.transport.playerMe().subscribe({
      next: result => {
        this.playerDetails = result.player;
        const player = this.playerRecord(result.player);
        if (player) this.state.acceptPlayer(player);
        this.startAuthenticatedStream();
      },
      error: error => {
        if (error instanceof HttpErrorResponse && error.status === 401) {
          this.state.markUnauthenticated();
          this.clearSession();
        }
        this.showError(error);
      },
      complete: () => this.busy = false
    });
  }

  private summaryFrom(value: unknown): GatewayPlayerSummary | null {
    const player = this.playerRecord(value);
    if (!player) return null;
    const id = Number(player['id'] ?? player['player_id']);
    return Number.isFinite(id) ? { id } : null;
  }

  private playerRecord(value: unknown): Record<string, unknown> | null {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
    const data = value as Record<string, unknown>;
    const nested = data['player'];
    return nested && typeof nested === 'object' && !Array.isArray(nested)
      ? nested as Record<string, unknown>
      : data;
  }

  private showError(error: unknown): void {
    this.busy = false;
    if (error instanceof HttpErrorResponse) {
      this.errorMessage = error.error?.error?.message ?? 'The gateway request failed.';
    } else {
      this.errorMessage = 'The gateway request failed.';
    }
  }

  private clearSession(): void {
    this.eventBridge?.disconnect(true);
    this.playerSummary = null;
    this.playerDetails = null;
    this.shipDetails = null;
    this.sectorDetails = null;
    this.portDetails = null;
    this.combatDetails = null;
    this.notices = [];
  }

  private async refreshPlayerState(): Promise<void> {
    this.state.beginRefresh(['player']);
    try {
      const result = await firstValueFrom(this.transport.playerMe());
      const player = this.playerRecord(result.player);
      if (player) {
        this.playerDetails = result.player;
        this.playerSummary = this.summaryFrom(result.player);
        this.state.acceptPlayer(player);
      }
    } catch {
      this.state.markAuthoritativeStateStale();
    }
  }

  private startAuthenticatedStream(): void {
    if (this.playerSummary) this.eventBridge?.connect(this.playerSummary.id);
  }

  private configureEventGatewayOrigin(origin: string): void {
    this.eventTransport?.configureGatewayOrigin?.(origin);
  }

  private runCommand(command: string, data: Record<string, unknown>, onSuccess?: (data: Record<string, unknown>) => void): void {
    if (!this.commands) {
      this.commandError = 'The gateway command transport is unavailable.';
      return;
    }
    this.commandError = '';
    this.commands.sendCommand(command, data).subscribe({
      next: response => {
        this.state.lastRpcRequest$.next({ command, data });
        this.state.lastRpcResponse$.next(response);
        this.state.updateFromRpc(response);
        if (!response || typeof response !== 'object') return;
        const envelope = response as Record<string, unknown>;
        if (envelope['status'] !== 'ok') {
          const error = envelope['error'];
          this.commandError = error && typeof error === 'object'
            ? String((error as Record<string, unknown>)['message'] ?? 'The game server refused the command.')
            : 'The game server refused the command.';
          return;
        }
        const payload = envelope['data'];
        if (payload && typeof payload === 'object' && !Array.isArray(payload)) {
          onSuccess?.(payload as Record<string, unknown>);
        }
      },
      error: error => this.commandError = error instanceof Error ? error.message : 'The gateway command failed.'
    });
  }

  private selectedPortId(): number | null {
    const id = this.clientState.port.value?.['port_id'] ?? this.clientState.port.value?.['id'];
    return Number.isInteger(Number(id)) ? Number(id) : null;
  }

  private validTradeQuantity(): number | null {
    const quantity = Number(this.tradeQuantity);
    return Number.isInteger(quantity) && quantity > 0 ? quantity : null;
  }
}
