import type { CapacitorConfig } from '@capacitor/cli';

// Set this only for local development, e.g. http://192.168.1.20:4200.
// Production builds package Angular assets and use a configured HTTPS gateway.
const devServerUrl = process.env['TWCLONE_CAPACITOR_DEV_SERVER_URL']?.trim();

const config: CapacitorConfig = {
  appId: 'com.twclone.game',
  appName: 'TWClone',
  webDir: 'dist/tw-web/browser',
  ...(devServerUrl ? {
    server: {
      url: devServerUrl,
      cleartext: devServerUrl.startsWith('http://')
    }
  } : {})
};

export default config;
