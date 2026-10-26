import { ApplicationConfig } from '@angular/core';
import { provideRouter } from '@angular/router';
import { provideHttpClient } from '@angular/common/http';
import {
  GatewayTransport,
  GameCommandTransport,
  ClientEventTransport,
  GatewayEventTransport,
  GatewayGameCommandTransport,
  TransportService
} from './services/transport.service';

import { routes } from './app.routes';

export const appConfig: ApplicationConfig = {
  providers: [
    provideRouter(routes),
    provideHttpClient(),
    { provide: GatewayTransport, useExisting: TransportService },
    { provide: ClientEventTransport, useExisting: GatewayEventTransport },
    { provide: GameCommandTransport, useExisting: GatewayGameCommandTransport }
  ]
};
