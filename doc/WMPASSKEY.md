WhatsApp Passkey Authentication
===============================
WhatsApp has introduced passkey-locked device linking for certain accounts.
For an affected account the WhatsApp servers refuse the normal QR-code /
pairing-code linking and instead require a passkey assertion, signed by
the account owner's own authenticator, before a new companion device
(such as nchat) may link.

Why a manual step is needed
---------------------------
A passkey assertion is cryptographically bound to:

- the account owner's authenticator (platform passkey, security key, or the
  owner's phone via hybrid/Bluetooth), and
- the `web.whatsapp.com` origin.

nchat cannot fabricate this assertion on its own, and neither can any other
headless client. The only thing that satisfies the server is the real owner, on
their own device/browser, using their own passkey. nchat therefore hands the
challenge out to a browser and asks you to paste the signed response back.

This only affects passkey-locked accounts. Accounts that still support QR /
pairing-code linking are unaffected and link as before.

How to link a passkey-locked account
-------------------------------------
When you run `nchat -s` (or otherwise trigger WhatsApp login) and the account
requires a passkey, nchat prints a challenge and a short JavaScript snippet.
Complete the following steps:

1. In a browser, open https://web.whatsapp.com signed in to (or able to
   authenticate with the passkey of) this WhatsApp account.

2. Open the browser developer console (F12, or Cmd-Opt-J on macOS) and run the
   snippet nchat printed. It looks like this:

       const opts = PublicKeyCredential.parseRequestOptionsFromJSON(<challenge>);
       const cred = await navigator.credentials.get({ publicKey: opts });
       console.log(JSON.stringify(cred.toJSON()));

3. Approve the passkey prompt (biometric / PIN / phone). The console prints a
   single line of JSON.

4. Copy that JSON line, paste it into the nchat prompt, and press Enter.

nchat sends the response to WhatsApp via `SendPasskeyResponse` and completes the
pairing. If the pairing requires an additional handoff confirmation, nchat shows
a short code and asks you to confirm it matches the code shown on your phone.

Notes
-----
- The console must be run on the `web.whatsapp.com` origin; a local HTML file or
  any other site will be rejected by the browser's WebAuthn origin check.
- `navigator.credentials.get()` may require a user gesture in some browsers. If
  the call fails from the console, trigger it from a button/`click` handler, or
  use a different browser.
- This flow uses your real passkey; it does not bypass any WhatsApp security.

Background: the underlying whatsmeow support was added in
[tulir/whatsmeow#1186](https://github.com/tulir/whatsmeow/pull/1186).
