import { TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { GatewayTransport, TransportService } from './transport.service';

describe('Gateway transport', () => {
  let service: TransportService;
  let http: HttpTestingController;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [provideHttpClient(), provideHttpClientTesting()]
    });
    service = TestBed.inject(TransportService);
    http = TestBed.inject(HttpTestingController);
  });

  afterEach(() => http.verify());

  it('uses the gateway login endpoint and keeps credentials in the request body', () => {
    let result: unknown;
    service.login('captain', 'secret').subscribe(value => result = value);
    const request = http.expectOne('/api/v1/auth/login');
    expect(request.request.method).toBe('POST');
    expect(request.request.withCredentials).toBeTrue();
    expect(request.request.body).toEqual({ username: 'captain', password: 'secret' });
    request.flush({ player: { id: 9 }, csrf_token: 'csrf-value' });
    expect(result).toEqual({ player: { id: 9 }, csrf_token: 'csrf-value' });
  });

  it('loads authoritative player state through the gateway using the cookie', () => {
    let result: unknown;
    service.playerMe().subscribe(value => result = value);
    const request = http.expectOne('/api/v1/player/me');
    expect(request.request.method).toBe('GET');
    expect(request.request.withCredentials).toBeTrue();
    request.flush({ player: { player: { id: 9 } } });
    expect(result).toEqual({ player: { player: { id: 9 } } });
  });

  it('recovers the CSRF value and sends it when logging out after a page reload', () => {
    let completed = false;
    service.logout().subscribe(() => completed = true);
    const csrf = http.expectOne('/api/v1/auth/csrf');
    expect(csrf.request.withCredentials).toBeTrue();
    csrf.flush({ csrf_token: 'restored-csrf' });
    const logout = http.expectOne('/api/v1/auth/logout');
    expect(logout.request.method).toBe('POST');
    expect(logout.request.withCredentials).toBeTrue();
    expect(logout.request.headers.get('X-CSRF-Token')).toBe('restored-csrf');
    logout.flush(null);
    expect(completed).toBeTrue();
  });

  it('provides the transport abstraction used by the web app', () => {
    expect(service instanceof GatewayTransport).toBeTrue();
  });
});
