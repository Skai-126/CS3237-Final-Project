import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// `npm run dev` serves the app on :5173 and forwards /api to Django on :8000
export default defineConfig({
  plugins: [react()],
  build: { chunkSizeWarningLimit: 700 }, // recharts is most of the bundle
  server: { proxy: { "/api": "http://127.0.0.1:8000" } },
});
