type VerifyPurpose = "register" | "link";

export interface EmailVerifyEnv {
	DB: D1Database;
	ACCOUNT_PEPPER: string;
	RESEND_API_KEY?: string;
	RESEND_FROM?: string;
	ENVIRONMENT?: string;
}

const VERIFY_TTL_SECONDS = 10 * 60;
const VERIFY_MAX_ATTEMPTS = 5;
const VERIFY_SEND_WINDOW_SECONDS = 15 * 60;
const VERIFY_SEND_LIMIT = 5;
const DEFAULT_FROM = "UClient <noreply@uclient.app>";

function json(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), {
		status,
		headers: {
			"content-type": "application/json; charset=utf-8",
			"cache-control": "no-store",
		},
	});
}

function error(status: number, code: string, message: string): Response {
	return json({error: code, message}, status);
}

export function mailLocale(value: unknown): "ko" | "en" {
	return typeof value === "string" && value.trim().toLowerCase().startsWith("ko") ? "ko" : "en";
}

function escapeHtml(value: string): string {
	return value.replace(/[&<>"']/g, char => ({
		"&": "&amp;",
		"<": "&lt;",
		">": "&gt;",
		'"': "&quot;",
		"'": "&#39;",
	}[char] ?? char));
}

function codeBoxesHtml(code: string): string {
	const digits = escapeHtml(code).split("");
	return `<table role="presentation" cellpadding="0" cellspacing="0" style="margin:0">
		<tr>${digits.map((digit, index) => `${index ? '<td width="8" style="width:8px;font-size:0;line-height:0">&nbsp;</td>' : ""}<td align="center" width="48" height="56" style="width:48px;height:56px;border:1px solid #e4e4e7;border-radius:12px;background:#fafafa;font:700 24px/56px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;color:#111111">${digit}</td>`).join("")}</tr>
	</table>`;
}

function verificationEmail(locale: "ko" | "en", code: string): {subject: string; text: string; html: string} {
	const boxed = codeBoxesHtml(code);
	if(locale === "ko") {
		return {
			subject: "UClient 인증 코드",
			text: `안녕하세요.\n\n아래 코드로 UClient 계정 확인을 완료하세요.\n\n코드: ${code}\n\n이 코드는 10분 동안 유효합니다. 본인이 요청하지 않았다면 이 메일을 무시하세요.`,
			html: verificationEmailHtml("ko", {
				eyebrow: "인증 코드",
				hello: "안녕하세요.",
				body: "아래 코드로 UClient 계정 확인을 완료하세요.",
				codeLabel: "코드",
				codeBoxes: boxed,
				expiry: "이 코드는 10분 동안 유효합니다.",
				ignore: "본인이 요청하지 않았다면 이 메일을 무시하세요.",
			}),
		};
	}
	return {
		subject: "UClient verification code",
		text: `Hello.\n\nUse this code to finish verifying your UClient account.\n\nCode: ${code}\n\nThis code expires in 10 minutes. If you did not request it, you can ignore this email.`,
		html: verificationEmailHtml("en", {
			eyebrow: "Verification code",
			hello: "Hello.",
			body: "Use this code to finish verifying your UClient account.",
			codeLabel: "Code",
			codeBoxes: boxed,
			expiry: "This code expires in 10 minutes.",
			ignore: "If you did not request it, you can ignore this email.",
		}),
	};
}

function verificationEmailHtml(locale: "ko" | "en", copy: {
	eyebrow: string;
	hello: string;
	body: string;
	codeLabel: string;
	codeBoxes: string;
	expiry: string;
	ignore: string;
}): string {
	return `<!DOCTYPE html>
<html lang="${locale}">
<body style="margin:0;padding:0;background:#f4f4f5">
  <table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background:#f4f4f5">
    <tr>
      <td align="center" style="padding:40px 16px">
        <table role="presentation" width="520" cellpadding="0" cellspacing="0" style="max-width:520px;width:100%;background:#ffffff;border-radius:16px">
          <tr>
            <td style="padding:40px 40px 32px">
              <div style="font:800 22px/1.1 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;letter-spacing:-.04em;color:#111111">UClient</div>
              <div style="padding-top:28px;font:700 12px/1.3 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;letter-spacing:.14em;text-transform:uppercase;color:#6b6b73">${escapeHtml(copy.eyebrow)}</div>
              <div style="padding-top:10px;font:700 26px/1.25 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#111111">${escapeHtml(copy.hello)}</div>
              <div style="padding-top:12px;font:400 15px/1.6 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#3f3f46">${escapeHtml(copy.body)}</div>
              <div style="padding-top:28px;font:700 12px/1.3 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;letter-spacing:.08em;text-transform:uppercase;color:#8a8a93">${escapeHtml(copy.codeLabel)}</div>
              <div style="padding-top:10px">${copy.codeBoxes}</div>
              <div style="padding-top:18px;font:400 13px/1.55 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#71717a">${escapeHtml(copy.expiry)} ${escapeHtml(copy.ignore)}</div>
            </td>
          </tr>
        </table>
        <div style="max-width:520px;padding:18px 8px 0;font:400 12px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#a1a1aa;text-align:left">UClient</div>
      </td>
    </tr>
  </table>
</body>
</html>`;
}

function sixDigitCode(): string {
	const bytes = crypto.getRandomValues(new Uint8Array(4));
	const value = ((bytes[0]! << 24) | (bytes[1]! << 16) | (bytes[2]! << 8) | bytes[3]!) >>> 0;
	return String(value % 1_000_000).padStart(6, "0");
}

async function sha256Hex(value: string): Promise<string> {
	const bytes = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value)));
	return Array.from(bytes, item => item.toString(16).padStart(2, "0")).join("");
}

export async function hashVerifyCode(code: string, pepper: string): Promise<string> {
	return sha256Hex(`${code}\0${pepper}`);
}

async function sendResendEmail(env: EmailVerifyEnv, to: string, locale: "ko" | "en", code: string): Promise<Response | null> {
	const key = env.RESEND_API_KEY?.trim() ?? "";
	if(!key)
		return null;
	const from = env.RESEND_FROM?.trim() || DEFAULT_FROM;
	const mail = verificationEmail(locale, code);
	const response = await fetch("https://api.resend.com/emails", {
		method: "POST",
		headers: {
			authorization: `Bearer ${key}`,
			"content-type": "application/json",
		},
		body: JSON.stringify({
			from,
			to: [to],
			subject: mail.subject,
			text: mail.text,
			html: mail.html,
		}),
	});
	if(!response.ok) {
		const detail = await response.text().catch(() => "");
		console.error(JSON.stringify({event: "resend_failed", status: response.status, detail: detail.slice(0, 300)}));
		return error(502, "email_send_failed", "Could not send the verification email.");
	}
	return null;
}

export async function startEmailVerification(
	env: EmailVerifyEnv,
	input: {
		email: string;
		purpose: VerifyPurpose;
		passwordHash: string;
		installId?: string;
		secretHash?: string;
		locale: "ko" | "en";
		version?: string;
		ip: string;
	},
): Promise<Response> {
	const now = Math.floor(Date.now() / 1000);
	await env.DB.prepare("DELETE FROM email_verifications WHERE expires_at <= ?1").bind(now).run();
	const recent = await env.DB.prepare(
		`SELECT COUNT(*) AS count FROM email_verifications
		 WHERE email_normalized = ?1 AND purpose = ?2 AND created_at > ?3`,
	).bind(input.email, input.purpose, now - VERIFY_SEND_WINDOW_SECONDS).first<{count: number}>();
	if((recent?.count ?? 0) >= VERIFY_SEND_LIMIT)
		return error(429, "auth_rate_limited", "Too many verification emails. Try again later.");

	const code = sixDigitCode();
	const codeHash = await hashVerifyCode(code, env.ACCOUNT_PEPPER);
	await env.DB.batch([
		env.DB.prepare("DELETE FROM email_verifications WHERE email_normalized = ?1 AND purpose = ?2")
			.bind(input.email, input.purpose),
		env.DB.prepare(
			`INSERT INTO email_verifications
			 (email_normalized, purpose, code_hash, password_hash, install_id, secret_hash, locale, version, expires_at, attempts, created_at)
			 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, 0, ?10)`,
		).bind(
			input.email,
			input.purpose,
			codeHash,
			input.passwordHash,
			input.installId ?? null,
			input.secretHash ?? null,
			input.locale,
			input.version ?? "",
			now + VERIFY_TTL_SECONDS,
			now,
		),
	]);

	const sendError = await sendResendEmail(env, input.email, input.locale, code);
	if(sendError)
		return sendError;

	const body: {ok: true; email: string; debug_code?: string} = {ok: true, email: input.email};
	if(!env.RESEND_API_KEY?.trim() && env.ENVIRONMENT !== "production")
		body.debug_code = code;
	return json(body, 202);
}

export async function loadEmailVerification(
	env: EmailVerifyEnv,
	email: string,
	purpose: VerifyPurpose,
	now: number,
): Promise<{
	id: number;
	code_hash: string;
	password_hash: string;
	install_id: string | null;
	secret_hash: string | null;
	version: string | null;
	attempts: number;
	expires_at: number;
} | null> {
	return await env.DB.prepare(
		`SELECT id, code_hash, password_hash, install_id, secret_hash, version, attempts, expires_at
		 FROM email_verifications
		 WHERE email_normalized = ?1 AND purpose = ?2
		 ORDER BY created_at DESC LIMIT 1`,
	).bind(email, purpose).first();
}

export async function consumeEmailVerification(
	env: EmailVerifyEnv,
	row: {id: number; code_hash: string; attempts: number; expires_at: number},
	code: string,
	pepper: string,
	now: number,
): Promise<Response | null> {
	if(row.expires_at <= now)
		return error(400, "code_expired", "This verification code has expired.");
	if(row.attempts >= VERIFY_MAX_ATTEMPTS)
		return error(429, "auth_rate_limited", "Too many verification attempts. Request a new code.");
	const actual = await hashVerifyCode(code.trim(), pepper);
	if(actual !== row.code_hash) {
		await env.DB.prepare("UPDATE email_verifications SET attempts = attempts + 1 WHERE id = ?1").bind(row.id).run();
		return error(403, "invalid_code", "That verification code is incorrect.");
	}
	await env.DB.prepare("DELETE FROM email_verifications WHERE id = ?1").bind(row.id).run();
	return null;
}
