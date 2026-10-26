import { Injectable } from '@angular/core';
import { App } from '@capacitor/app';
import { Network } from '@capacitor/network';
import type { PluginListenerHandle } from '@capacitor/core';

@Injectable({ providedIn: 'root' })
export class ClientLifecycleService {
  private handles: PluginListenerHandle[] = [];
  private started = false;

  start(onResume: () => void, onPause: () => void): void {
    this.stop();
    this.started = true;
    const visibility = () => document.hidden ? onPause() : onResume();
    document.addEventListener('visibilitychange', visibility);
    this.handles.push({ remove: async () => document.removeEventListener('visibilitychange', visibility) });

    void App.addListener('appStateChange', ({ isActive }) => isActive ? onResume() : onPause())
      .then(handle => this.keep(handle));
    void App.addListener('backButton', ({ canGoBack }) => {
      if (canGoBack) globalThis.history.back();
      // On the authenticated root screen, Android back must not silently log
      // out or mutate game state. The OS may still close the app normally.
    }).then(handle => this.keep(handle));
    void Network.addListener('networkStatusChange', ({ connected }) => connected ? onResume() : onPause())
      .then(handle => this.keep(handle));
    void Network.getStatus().then(status => status.connected ? onResume() : onPause()).catch(() => undefined);
  }

  stop(): void {
    if (!this.started) return;
    this.started = false;
    for (const handle of this.handles.splice(0)) void handle.remove();
  }

  private keep(handle: PluginListenerHandle): void {
    if (this.started) this.handles.push(handle);
    else void handle.remove();
  }
}
