import {generateKeyPairSync} from "node:crypto";

const {privateKey, publicKey} = generateKeyPairSync("ed25519");
const privateJwk = privateKey.export({format: "jwk"});
const publicJwk = publicKey.export({format: "jwk"});

function base64UrlToBuffer(value) {
	return Buffer.from(value.replace(/-/g, "+").replace(/_/g, "/"), "base64");
}

const seed = base64UrlToBuffer(privateJwk.d);
const publicKeyBytes = base64UrlToBuffer(publicJwk.x);

console.log(`GRACE_PRIVATE_KEY_SEED_HEX=${seed.toString("hex")}`);
console.log(`UCLIENT_GRACE_PUBLIC_KEY_HEX=${publicKeyBytes.toString("hex")}`);
console.log("Keep the private seed only in the Worker secret. Embed the public key in account.cpp.");
