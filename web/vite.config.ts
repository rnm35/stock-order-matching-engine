import react from "@vitejs/plugin-react";
import { defineConfig } from "vitest/config";

export default defineConfig({
    plugins: [react()],
    // Relative asset URLs, so the built site works from any path (e.g. a subdirectory).
    base: "./",
    test: {
        environment: "node",
        include: ["src/**/*.test.ts", "mcp/**/*.test.ts"],
    },
});
