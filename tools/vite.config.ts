import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const pad2 = (value: number) => String(value).padStart(2, '0');
const now = new Date();
const buildTime = [
  now.getFullYear(),
  pad2(now.getMonth() + 1),
  pad2(now.getDate()),
].join('/') + ` ${pad2(now.getHours())}:${pad2(now.getMinutes())}`;

export default defineConfig({
  plugins: [react()],
  define: {
    __APP_BUILD_TIME__: JSON.stringify(buildTime),
  },
  server: {
    port: 4173,
  },
});
