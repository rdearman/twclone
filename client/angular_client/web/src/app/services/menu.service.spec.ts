import { TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { GameCommandTransport, PendingGameCommandTransport } from './transport.service';

import { MenuService } from './menu.service';

describe('MenuService', () => {
  let service: MenuService;
  let http: HttpTestingController;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [
        provideHttpClient(),
        provideHttpClientTesting(),
        { provide: GameCommandTransport, useClass: PendingGameCommandTransport }
      ]
    });
    http = TestBed.inject(HttpTestingController);
    service = TestBed.inject(MenuService);
    http.expectOne('assets/menus.json').flush({});
  });

  afterEach(() => http.verify());

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
