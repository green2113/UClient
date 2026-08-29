import {cloudflareTest, readD1Migrations} from "@cloudflare/vitest-plugin";
import {defineConfig} from "vitest/config";

declare global {
	interface ImportMeta {
		readonly url: string;
	}
}

function fileUrlPath(url: URL): string {
	return decodeURIComponent(url.pathname).replace(/^\/([A-Za-z]:)/, "$1");
}

export default defineConfig({
	plugins: [
		cloudflareTest(async () => ({
			wrangler: {configPath: "./wrangler.jsonc"},
			miniflare: {
				bindings: {
					ACCOUNT_PEPPER: "test-account-pepper-not-for-production",
					GRACE_PRIVATE_KEY_SEED_HEX: "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
					RELAY_SECRET: "test-relay-secret-not-for-production",
					TEST_MIGRATIONS: await readD1Migrations(fileUrlPath(new URL("./migrations", import.meta.url))),
				},
			},
		})),
	],
	test: {
		include: ["test/**/*.test.ts"],
	},
});
