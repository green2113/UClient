# UClient rooms service

Cloudflare Worker and D1 backend for UClient accounts, bans, and chat rooms.

## Local setup

1. Run `npm install`.
2. Copy `.dev.vars.example` to `.dev.vars`.
3. Run `npm run keygen`. Put the private seed in `.dev.vars` and embed the printed public key in the UClient account component.
4. Run `npx wrangler d1 migrations apply uclient-rooms --local`.
5. Run `npm run dev`.

The `database_id` in `wrangler.jsonc` is a local placeholder. Replace it with the ID returned by `npx wrangler d1 create uclient-rooms` before deploying.

## Production secrets

Set secrets without putting them in source control:

```text
npx wrangler secret put ACCOUNT_PEPPER
npx wrangler secret put GRACE_PRIVATE_KEY_SEED_HEX
npx wrangler secret put RELAY_SECRET
npx wrangler secret put RELAY_INVALIDATE_SECRET
npx wrangler secret put RESEND_API_KEY
```

`RESEND_FROM` is a Worker variable, default `UClient <noreply@uclient.app>`. Verify the domain in Resend and add DNS records for `uclient.app` before sending production mail. Without `RESEND_API_KEY`, local/dev start responses include `debug_code` so tests can confirm signup without sending mail.

`RELAY_INVALIDATE_URL` is a non-secret Worker variable containing the relay's `/internal/rooms/invalidate` URL.

## Manual bans

`expires_at` is a Unix timestamp in seconds. Use `NULL` for a permanent ban.

Admin UI:

- `GET /admin/bans` — password-protected ban page (same `ADMIN_TOKEN` as notices)
- `GET /admin/accounts/search?q=` — find an account by install id, email, player name, or last IP
- `GET /admin/bans/active` — active bans
- `POST /admin/bans` — `{install_id, reason, expires_at}` (`expires_at` null is permanent)
- `DELETE /admin/bans/:install_id` — remove the active ban

Open `https://uclient.under1111.com/admin/bans`, sign in with the admin token, search the account, and ban it. The launcher then blocks Play, and the game client stays on the blocked screen until the ban ends.

SQL still works:

```sql
INSERT INTO user_bans(install_id, reason, banned_at, expires_at, banned_by)
VALUES(
	'00000000-0000-0000-0000-000000000000',
	'Reason in English',
	unixepoch(),
	unixepoch() + 86400,
	'admin'
);
```

Remove the active ban for an account:

```sql
DELETE FROM user_bans WHERE install_id = '00000000-0000-0000-0000-000000000000';
```

Run SQL remotely with `npx wrangler d1 execute uclient-rooms --remote --command "..."`.

## Launcher notices

Public endpoint:

- `GET /launcher/notices` — active notices for the UClient launcher

Admin UI:

- `GET /admin` — password-protected notice management page
- `GET/POST/PATCH/DELETE /admin/notices` — CRUD API (`Authorization: Bearer <ADMIN_TOKEN>`)

Set the admin token as a Worker secret:

```bash
npx wrangler secret put ADMIN_TOKEN
```

Then open `https://uclient.under1111.com/admin`, sign in with the token, and create notices. Use **Blocks Play** for maintenance windows that should disable the launcher Play button.

## Launcher assistant

Authenticated endpoint:

- `POST /ai/chat` — streams the UClient personal assistant (`Authorization: Bearer` + `x-uclient-install-id`)

The Worker calls Amazon Bedrock `gpt-oss-120b` through `bedrock-runtime` Chat Completions (`openai.gpt-oss-120b-1:0`). GPT-5.6 Terra/Luna/Sol are often listed in the console but still return `not available for this account` until AWS grants them. Console long-term API keys use `AmazonBedrockLimitedAccess`, which works on that endpoint. Put the key in a secret, not in source:

```bash
npx wrangler secret put BEDROCK_API_KEY
```

Generate the key in **N. Virginia (us-east-1)** → Bedrock → API keys → Long-term. Do not wrap it in quotes.

The Worker is placed near `aws:us-east-1` so GPT-5.x OpenAI geo checks see a US caller. Korean users often hit the Hong Kong colo otherwise, and OpenAI blocks Hong Kong. Open-weight `gpt-oss` is sold by AWS and does not use that OpenAI country list.

`BEDROCK_REGION`, `BEDROCK_ENDPOINT`, and `BEDROCK_MODEL_ID` are wrangler vars. Mantle needs `AmazonBedrockMantleInferenceAccess` on the key's IAM user. Rebuild the cached catalog after shortcut or config changes:

```bash
python ../../scripts/generate_ai_knowledge.py
```

