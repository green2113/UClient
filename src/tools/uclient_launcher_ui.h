// UClient launcher UI, rendered by WebView2. The markup is embedded in the
// executable so the launcher stays a single self-contained binary.
//
// Contract with the host (uclient_launcher.cpp):
//   C++ -> JS   window.__setState(stateObject)
//   JS  -> C++  window.chrome.webview.postMessage(JSON.stringify({cmd: ...}))
//
// State fields: phase, buttonLabel, version, status, percent, failed,
//               autoLaunch, autoUpdate, discordRpc, logoUrl, mascotUrl, friendsLoading, friendsLoaded,
//               playBlocked, updateAvailable, gameRunning, buttonHint,
//               devBuild, devForceUpdate, devForcePlayBlocked, devForceGameRunning, devInjectNotice,
//               notices[] {id, title, body, severity, blocksPlay, expiresAt?},
//               updateStage, downloadDone, downloadTotal, downloadSpeed, etaSeconds,
//               friends[] {name, clan, online, server, map, address}
//
// Kept strictly ASCII: this is a wide literal and the file may be compiled
// without /utf-8, so any non-ASCII glyph must be a \u escape inside JS.
#pragma once

static const wchar_t *const kLauncherHtml = LR"HTMLDOC(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src https://uclient.local data:; font-src https://cdn.jsdelivr.net data:; style-src 'unsafe-inline'; script-src 'unsafe-inline'">
<title>UClient</title>
<style>
@font-face{
  font-family:'Pretendard';
  font-weight:800;
  font-style:normal;
  font-display:swap;
  src:url('https://cdn.jsdelivr.net/gh/orioncactus/pretendard@v1.3.9/packages/pretendard/dist/public/static/Pretendard-ExtraBold.woff2') format('woff2');
}
:root{
  --accent:#7c6cf0;
  --accent-hi:#9b8ef8;
  --accent-deep:#5a4ad8;
  --accent-glow:rgba(124,108,240,.48);
  --on-accent:#ffffff;
  --text:#f5f5f7;
  --dim:#a9abb4;
  --muted:#74767f;
  --line:rgba(255,255,255,.08);
  --panel:rgba(20,22,28,.72);
  --row-hover:rgba(255,255,255,.06);
  --ease:cubic-bezier(.22,.61,.36,1);
  --win-r:12px;
  --play-radius:22px;
  --play-radius-compact:20px;
  --play-fg:#ffffff;
}
*{margin:0;padding:0;box-sizing:border-box}
html,body{
  height:100%;overflow:hidden;
  border-radius:var(--win-r);
}
body{
  background:#06070a;
  color:var(--text);
  font:400 15px/1.45 "Segoe UI Variable Text","Segoe UI",system-ui,sans-serif;
  -webkit-font-smoothing:antialiased;
  user-select:none;
  cursor:default;
}

/* -- backdrop ---------------------------------------------------------- */
#bg{position:fixed;inset:0;overflow:hidden;background:linear-gradient(160deg,#12141c 0%,#08090d 55%,#050508 100%)}
#bg i{position:absolute;display:block;border-radius:50%;filter:blur(70px);opacity:.5;will-change:transform}
#bg .b1{width:620px;height:620px;right:-160px;top:-220px;background:#2f7fd0;animation:drift1 24s var(--ease) infinite alternate}
#bg .b2{width:560px;height:560px;left:60px;bottom:-260px;background:#5b4bd4;opacity:.32;animation:drift2 30s var(--ease) infinite alternate}
#bg .b3{width:420px;height:420px;left:44%;top:16%;background:#8d2350;opacity:.3;animation:drift3 27s var(--ease) infinite alternate}
#bg .grain{position:absolute;inset:0;filter:none;border-radius:0;opacity:.5;
  background:radial-gradient(120% 90% at 50% 0%,transparent 40%,rgba(0,0,0,.55) 100%)}
@keyframes drift1{to{transform:translate3d(-70px,60px,0) scale(1.12)}}
@keyframes drift2{to{transform:translate3d(90px,-50px,0) scale(1.08)}}
@keyframes drift3{to{transform:translate3d(-40px,70px,0) scale(1.15)}}

/* -- shell ------------------------------------------------------------- */
#shell{position:relative;height:100%;display:grid;grid-template-columns:76px 1fr 372px}
#titlebar{position:fixed;top:0;left:76px;right:0;height:56px;z-index:40;display:flex;justify-content:flex-end;align-items:center;gap:4px;padding:0 12px 0 0}
#drag{position:absolute;inset:0 108px 0 0}
.capbtn{
  width:40px;height:34px;border:0;background:transparent;border-radius:10px;
  display:grid;place-items:center;cursor:pointer;color:var(--dim);
  transition:background .16s var(--ease),color .16s var(--ease),transform .12s var(--ease);
}
.capbtn:hover{background:rgba(255,255,255,.09);color:#fff}
.capbtn:active{transform:scale(.92)}
#btn-close:hover{background:#e13b3b;color:#fff}

/* -- rail -------------------------------------------------------------- */
#rail{
  position:relative;z-index:30;background:rgba(9,9,13,.82);
  border-right:1px solid var(--line);backdrop-filter:blur(18px);
  display:flex;flex-direction:column;align-items:center;padding:48px 0 18px;
}
#mascot{width:34px;height:34px;object-fit:contain;margin-top:6px;
  filter:drop-shadow(0 3px 10px rgba(0,0,0,.6));animation:pop .6s var(--ease) both}
#rail .spacer{flex:1}
#btn-gear{
  width:44px;height:44px;border:0;background:transparent;border-radius:13px;color:var(--muted);
  display:grid;place-items:center;cursor:pointer;position:relative;
  transition:background .18s var(--ease),color .18s var(--ease);
}
#btn-gear svg{transition:transform .5s var(--ease)}
#btn-gear:hover{background:rgba(255,255,255,.07);color:#fff}
#btn-gear:hover svg{transform:rotate(60deg)}
#btn-gear.on{color:#fff;background:rgba(124,108,240,.16)}
#btn-gear.on::before{content:"";position:absolute;left:-14px;top:11px;width:3px;height:22px;border-radius:2px;background:var(--accent)}

/* -- main -------------------------------------------------------------- */
#main{position:relative;z-index:20;padding:56px 34px 34px 44px;display:flex;flex-direction:column;min-width:0}
#tabs{position:relative;align-self:center;display:flex;background:rgba(255,255,255,.045);border:1px solid var(--line);border-radius:999px;padding:5px;backdrop-filter:blur(10px)}
.tab{
  position:relative;z-index:1;border:0;background:transparent;cursor:pointer;
  font:600 15px/1 inherit;color:var(--muted);padding:10px 26px;border-radius:999px;
  transition:color .2s var(--ease);
}
.tab.on{color:#fff}
#tabpill{position:absolute;top:5px;left:5px;height:calc(100% - 10px);border-radius:999px;background:rgba(255,255,255,.09);
  box-shadow:inset 0 0 0 1px rgba(255,255,255,.07);transition:transform .32s var(--ease),width .32s var(--ease)}

.view{flex:1;min-height:0;display:none;flex-direction:column;padding-top:52px}
.view.on{display:flex;animation:viewin .34s var(--ease) both}
@keyframes viewin{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}}

#logo{max-width:440px;width:100%;height:auto;filter:drop-shadow(0 10px 34px rgba(0,0,0,.65));animation:pop .7s .06s var(--ease) both}
#logofb{font:800 46px/1 inherit;letter-spacing:-1px;display:none}
#ov-meta{margin-top:auto}
#ov-status{color:var(--muted);font-size:14px;min-height:20px;transition:color .2s var(--ease)}
#ov-status.bad{color:#ff7a6e}

#up-title{font:800 40px/1 inherit;letter-spacing:-.6px}
#up-version{color:var(--dim);font-size:19px;margin-top:16px}
#up-state{color:var(--dim);font-size:15px;margin-top:22px}
#up-status{color:var(--muted);font-size:14px;margin-top:8px;min-height:20px}
#up-status.bad{color:#ff7a6e}
.bar{margin-top:26px;width:min(460px,100%);height:8px;border-radius:99px;background:rgba(255,255,255,.08);overflow:hidden;display:none}
.bar.on{display:block}
.bar>span{display:block;height:100%;border-radius:99px;background:linear-gradient(90deg,var(--accent-deep),var(--accent),var(--accent-hi));
  box-shadow:0 0 14px var(--accent-glow);transition:width .3s var(--ease)}
.bar.indet>span{width:38%!important;animation:sweep 1.35s var(--ease) infinite}
@keyframes sweep{0%{transform:translateX(-110%)}100%{transform:translateX(300%)}}

/* -- settings modal ---------------------------------------------------- */
.modal{position:fixed;inset:0;z-index:200;display:flex;align-items:center;justify-content:center;
  pointer-events:none;opacity:0;transition:opacity .26s var(--ease)}
.modal.on{pointer-events:auto;opacity:1}
.modal-dim{position:absolute;inset:0;background:rgba(0,0,0,.62);backdrop-filter:blur(10px)}
.modal-box{
  position:relative;z-index:1;width:min(680px,90vw);height:min(440px,72vh);
  background:linear-gradient(160deg,#1a1c24 0%,#12141a 100%);
  border:1px solid rgba(255,255,255,.1);border-radius:14px;
  display:grid;grid-template-columns:188px 1fr;overflow:hidden;
  box-shadow:0 28px 80px rgba(0,0,0,.75),0 0 0 1px rgba(255,255,255,.04) inset;
  transform:translateY(28px) scale(.96);transition:transform .32s var(--ease);
}
.modal.on .modal-box{transform:none}
.modal-side{padding:28px 20px;border-right:1px solid var(--line);display:flex;flex-direction:column}
.modal-side h2{font:700 26px/1.1 inherit;letter-spacing:-.4px}
.modal-nav{margin-top:28px;display:flex;flex-direction:column;gap:4px}
.modal-nav button{
  border:0;background:transparent;text-align:left;cursor:pointer;
  font:600 14px/1 inherit;color:var(--dim);padding:10px 14px;border-radius:10px;
  transition:background .15s var(--ease),color .15s var(--ease);
}
.modal-nav button.on{background:rgba(255,255,255,.07);color:#fff}
.modal-body{padding:28px 32px 24px;display:flex;flex-direction:column;min-width:0}
.modal-head{display:flex;align-items:center;justify-content:space-between;margin-bottom:28px}
.modal-head h3{font:700 22px/1 inherit}
.modal-x{
  width:34px;height:34px;border:0;border-radius:9px;background:transparent;
  color:var(--dim);cursor:pointer;display:grid;place-items:center;
  transition:background .15s var(--ease),color .15s var(--ease);
}
.modal-x:hover{background:rgba(255,255,255,.08);color:#fff}
.sec{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.1em;margin:0 0 14px}
.opt{display:flex;align-items:flex-start;gap:16px;padding:16px;border-radius:14px;cursor:pointer;
  border:1px solid transparent;transition:background .16s var(--ease),border-color .16s var(--ease)}
.opt:hover{background:rgba(255,255,255,.04);border-color:var(--line)}
.sw{flex:0 0 auto;width:46px;height:26px;border-radius:99px;background:rgba(255,255,255,.13);position:relative;transition:background .22s var(--ease);margin-top:2px}
.sw::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;
  box-shadow:0 2px 6px rgba(0,0,0,.5);transition:transform .24s var(--ease)}
.opt.on .sw{background:var(--accent)}
.opt.on .sw::after{transform:translateX(20px)}
.opt b{display:block;font-weight:600;font-size:15px}
.opt small{display:block;color:var(--muted);font-size:13px;margin-top:4px}

/* -- play -------------------------------------------------------------- */
#play-stack{
  margin-top:auto;align-self:flex-start;display:flex;flex-direction:column;align-items:flex-start;gap:10px;
  transform:translateY(-30px);
}
#play-wrap{
  position:relative;display:inline-flex;
}
#play-hint-flyout{display:none}
#play-wrap.hint-on #play-hint-flyout{
  display:block;
  position:absolute;left:calc(100% + 10px);top:50%;transform:translateY(-50%);
  width:min(360px,72vw);opacity:0;visibility:hidden;pointer-events:none;
  transition:opacity .18s var(--ease),visibility .18s var(--ease);z-index:60;
}
#play-wrap.hint-on #play-hint-flyout::before{
  content:"";position:absolute;right:100%;top:0;width:10px;height:100%;
}
#play-wrap.hint-on:hover #play-hint-flyout,#play-wrap.hint-on:focus-within #play-hint-flyout{
  opacity:1;visibility:visible;pointer-events:auto;
}
#play-hint-flyout-box{
  background:#1e2028;border:1px solid rgba(255,255,255,.12);border-radius:12px;
  box-shadow:0 18px 48px rgba(0,0,0,.68);padding:16px 18px;max-height:min(280px,44vh);
}
#play-hint-flyout-inner{
  color:var(--dim);font-size:14px;line-height:1.55;white-space:pre-wrap;overflow-y:auto;
  max-height:min(240px,38vh);
}
#play{
  position:relative;z-index:1;
  display:inline-flex;align-items:stretch;justify-content:center;
  border:0;cursor:pointer;overflow:hidden;box-sizing:border-box;
  font-family:'Pretendard',"Segoe UI",system-ui,sans-serif;
  font-weight:700;font-size:22px;letter-spacing:-.02em;line-height:1;
  transition:
    width .38s var(--ease),height .38s var(--ease),min-width .38s var(--ease),
    border-radius .34s var(--ease),background .3s var(--ease),box-shadow .3s var(--ease),
    transform .18s var(--ease),filter .22s var(--ease);
  animation:pop .6s .12s var(--ease) both;
}
#play.mode-play{
  height:58px;padding:0;
  border-radius:var(--play-radius);
  color:var(--play-fg);font-size:28px;
  background:linear-gradient(180deg,var(--accent-hi) 0%,var(--accent) 52%,var(--accent-deep) 100%);
  box-shadow:0 4px 18px -3px var(--accent-glow);
}
#play.mode-play:hover:not(:disabled){
  transform:translateY(-1px);
  filter:brightness(1.08);
  box-shadow:0 6px 22px -2px var(--accent-glow),0 0 24px var(--accent-glow);
}
#play.mode-play:active:not(:disabled){
  transform:translateY(0) scale(.985);
  box-shadow:0 3px 14px -3px var(--accent-glow);
  filter:none;
}
#play:disabled{cursor:default}
#play.mode-play:disabled,#play.mode-update:disabled{
  background:rgba(255,255,255,.07);color:var(--muted);box-shadow:none;animation:none;filter:none;transform:none;
}
#play.mode-play:disabled .play-ico,#play.mode-update:disabled .play-ico{color:var(--muted);opacity:.65}

/* Stacked faces — inactive absolute; active face sizes the button */
.play-normal,.play-update,.play-checking,.play-progress,.play-running{
  position:absolute;inset:0;display:flex;align-items:center;justify-content:center;
  opacity:0;visibility:hidden;pointer-events:none;
  transform:translateY(8px) scale(.96);
  transition:opacity .24s var(--ease),visibility .24s var(--ease),transform .32s var(--ease);
}
#play.mode-play .play-normal,
#play.mode-update .play-update,
#play.mode-running .play-running{
  position:relative;inset:auto;flex:0 0 auto;
}
.play-checking,.play-progress{
  flex-direction:column;align-items:stretch;justify-content:center;
  padding:12px 18px 14px;gap:8px;
}
#play.mode-play .play-normal,
#play.mode-update .play-update,
#play.mode-checking .play-checking,
#play.mode-progress .play-progress,
#play.mode-running .play-running{
  opacity:1;visibility:visible;pointer-events:auto;transform:none;
}
#play.mode-progress .play-progress{transition-delay:.14s}
#play.mode-checking .play-checking{transition-delay:.1s}
#play.mode-progress .play-update,
#play.mode-progress .play-normal,
#play.mode-checking .play-update,
#play.mode-checking .play-normal,
#play.mode-checking .play-progress{
  transition-delay:0s;opacity:0;visibility:hidden;
  transform:translateY(-6px) scale(.97);
}
#play.mode-update .play-update,
#play.mode-play .play-normal{transition-delay:.08s}

/* Normal play / update layout */
.play-normal,.play-update{
  flex-direction:row;align-items:center;justify-content:center;
  gap:10px;padding:0 38px;box-sizing:border-box;white-space:nowrap;
}
#play .play-ico{
  flex:0 0 auto;width:32px;height:32px;display:grid;place-items:center;color:var(--play-fg);
}
#play .play-ico svg{display:block;width:32px;height:32px}
#play-label,#update-label{display:block;line-height:1}

/* Manual update layout */
#play.mode-update{
  height:58px;padding:0;
  border-radius:var(--play-radius);
  color:var(--play-fg);font-size:28px;
  background:linear-gradient(180deg,var(--accent-hi) 0%,var(--accent) 52%,var(--accent-deep) 100%);
  box-shadow:0 4px 18px -3px var(--accent-glow);
}
#play.mode-update:hover:not(:disabled){
  transform:translateY(-1px);filter:brightness(1.08);
  box-shadow:0 6px 22px -2px var(--accent-glow),0 0 24px var(--accent-glow);
}
#play.mode-update:active:not(:disabled){
  transform:translateY(0) scale(.985);box-shadow:0 3px 14px -3px var(--accent-glow);filter:none;
}

/* Running — text only */
#play.mode-running{
  height:58px;padding:0;
  border-radius:var(--play-radius);
  color:var(--play-fg);font-size:28px;font-weight:700;letter-spacing:.04em;
  background:rgba(255,255,255,.08);box-shadow:inset 0 1px 0 rgba(255,255,255,.06);animation:none;
}
.play-running{padding:0 38px;box-sizing:border-box;white-space:nowrap}

/* Checking — indeterminate bar + label */
#play.mode-checking{
  width:min(360px,92vw);min-width:280px;height:64px;padding:0;
  border-radius:var(--play-radius-compact);
  background:linear-gradient(180deg,#2a2640 0%,#181622 100%);
  color:#ece8ff;box-shadow:inset 0 1px 0 rgba(255,255,255,.06);animation:none;
}
.pbar-top{height:3px;background:rgba(255,255,255,.14);border-radius:99px;overflow:hidden;flex:0 0 auto}
.pbar-top>span{display:block;height:100%;background:var(--accent);border-radius:99px;transition:width .25s var(--ease);
  box-shadow:0 0 10px var(--accent-glow)}
.pbar-top.indet>span{width:38%!important;animation:sweep 1.25s var(--ease) infinite}
.play-check-label{font-size:14px;font-weight:700;text-align:left;letter-spacing:-.01em}

/* Download / apply — stats row like Valorant */
#play.mode-progress{
  width:min(400px,94vw);min-width:300px;height:68px;padding:0;
  border-radius:var(--play-radius-compact);
  background:linear-gradient(180deg,#2a2640 0%,#181622 100%);
  color:#ece8ff;box-shadow:inset 0 1px 0 rgba(255,255,255,.06);animation:none;
}
.prog-row{display:flex;align-items:center;justify-content:space-between;gap:8px;
  font-size:12px;font-weight:700;letter-spacing:.01em;color:rgba(236,230,216,.92)}
.prog-row span{white-space:nowrap}
.prog-speed{text-align:center;flex:1;color:var(--accent-hi)}
.prog-label{font-size:11px;font-weight:600;text-align:left;color:var(--muted);letter-spacing:.04em;text-transform:uppercase}

#play-version{color:var(--dim);font-size:14px;line-height:1.3;padding-left:2px}
@keyframes sweep{0%{transform:translateX(-120%)}100%{transform:translateX(320%)}}

/* -- alerts ------------------------------------------------------------ */
#alert-wrap{
  position:relative;display:none;align-self:flex-start;max-width:min(460px,100%);
}
#alert-wrap.on{display:block}
#alert-strip{
  display:flex;align-items:center;gap:9px;padding:7px 12px;border:0;border-radius:8px;
  background:transparent;color:#fff;font:inherit;text-align:left;cursor:default;outline:none;
  transition:background .18s var(--ease);
}
#alert-wrap:hover #alert-strip,#alert-wrap:focus-within #alert-strip{
  background:rgba(255,255,255,.09);
}
#alert-strip .alert-ico{
  flex:0 0 auto;width:22px;height:22px;display:grid;place-items:center;
  color:#f5c518;
}
#alert-strip.sev-critical .alert-ico{color:#ef4444}
#alert-strip.sev-info .alert-ico{color:#9b8ef8}
#alert-strip .alert-ico svg{display:block;width:22px;height:22px}
#alert-strip .alert-ico .ico-mark{fill:#111111}
#alert-strip .alert-title{
  font:700 15px/1.25 inherit;letter-spacing:-.01em;color:#fff;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
}
#alert-flyout{
  position:absolute;left:calc(100% + 10px);top:0;
  width:min(360px,72vw);opacity:0;visibility:hidden;pointer-events:none;
  transition:opacity .18s var(--ease),visibility .18s var(--ease);
  z-index:60;
}
#alert-flyout::before{
  content:"";position:absolute;right:100%;top:0;width:10px;height:100%;
}
#alert-wrap:hover #alert-flyout,#alert-wrap:focus-within #alert-flyout{
  opacity:1;visibility:visible;pointer-events:auto;
}
#alert-flyout-box{
  position:relative;
  background:#1e2028;
  border:1px solid rgba(255,255,255,.12);border-radius:12px;
  box-shadow:0 18px 48px rgba(0,0,0,.68);
  padding:16px 18px;display:flex;flex-direction:column;
  max-height:min(280px,44vh);
}
#alert-flyout-inner{
  overflow-y:auto;overscroll-behavior:contain;padding-right:4px;
  max-height:min(240px,38vh);
}
#alert-flyout-inner::-webkit-scrollbar{width:8px}
#alert-flyout-inner::-webkit-scrollbar-thumb{background:rgba(255,255,255,.14);border-radius:8px}
.notice-expiry{margin:10px 0 0;color:var(--muted);font-size:13px;line-height:1.45}
.notice-expiry b{color:var(--dim);font-weight:600}
.notice-block+.notice-block{margin-top:14px;padding-top:14px;border-top:1px solid var(--line)}
.notice-block h4{font:700 16px/1.25 inherit;letter-spacing:-.01em;color:#fff;margin-bottom:8px}
.notice-block p{color:var(--dim);font-size:14px;line-height:1.55;white-space:pre-wrap}

/* -- dev panel (DevRelease only; hidden unless devBuild) ---------------- */
#dev-panel{
  position:fixed;left:14px;bottom:14px;z-index:400;display:none;
  width:min(280px,88vw);background:rgba(10,12,18,.94);border:1px solid rgba(255,200,80,.35);
  border-radius:14px;box-shadow:0 16px 40px rgba(0,0,0,.55);backdrop-filter:blur(12px);
  font-size:12px;color:#d8dce8;touch-action:none;
}
body.dev-build #dev-panel{display:block}
#dev-panel.collapsed{width:auto;min-width:210px}
#dev-panel.collapsed #dev-body{display:none}
#dev-head{
  width:100%;display:flex;align-items:center;gap:8px;
  padding:9px 10px 9px 8px;border:0;border-bottom:1px solid rgba(255,200,80,.18);
  background:transparent;color:#ffd56a;font:700 12px/1 inherit;cursor:grab;user-select:none;
}
#dev-panel.collapsed #dev-head{border-bottom:0}
#dev-head:active{cursor:grabbing}
.dev-grip{
  flex:0 0 auto;width:14px;color:rgba(255,213,106,.55);font-size:14px;line-height:1;
  letter-spacing:-2px;text-align:center;
}
.dev-title{flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#dev-head small{color:#9aa0b0;font-weight:600;flex:0 0 auto}
#dev-collapse{
  flex:0 0 auto;width:24px;height:24px;border:0;border-radius:7px;
  background:rgba(255,255,255,.06);color:#d8dce8;font:700 14px/1 inherit;cursor:pointer;
}
#dev-collapse:hover{background:rgba(255,255,255,.12)}
#dev-panel.collapsed #dev-collapse{transform:rotate(-90deg)}
#dev-body{padding:10px 12px 12px;display:flex;flex-direction:column;gap:8px}
.dev-row{display:flex;align-items:center;justify-content:space-between;gap:10px}
.dev-row label{display:flex;align-items:center;gap:8px;cursor:pointer;color:#e8ebf5}
.dev-row input{accent-color:#ffd56a;width:14px;height:14px}
.dev-actions{display:flex;flex-wrap:wrap;gap:6px;margin-top:4px}
.dev-actions button{
  border:1px solid rgba(255,255,255,.14);border-radius:8px;padding:6px 10px;
  background:rgba(255,255,255,.06);color:#f3f4f8;font:600 11px/1 inherit;cursor:pointer;
}
.dev-actions button:hover{background:rgba(255,255,255,.12)}
.dev-actions button.warn{border-color:rgba(255,120,80,.45);color:#ffc9b8}

/* -- friends ----------------------------------------------------------- */
#friends{position:relative;z-index:20;margin:52px 24px 24px 0;background:var(--panel);border:1px solid var(--line);
  border-radius:20px;backdrop-filter:blur(26px);display:flex;flex-direction:column;overflow:hidden;
  box-shadow:0 24px 60px -24px rgba(0,0,0,.8);animation:pop .6s .04s var(--ease) both}
#fr-head{padding:24px 24px 16px;border-bottom:1px solid var(--line)}
#fr-title{display:flex;align-items:center;justify-content:space-between}
#fr-head h2{font:600 22px/1 inherit}
#fr-refresh{width:34px;height:34px;border:0;border-radius:10px;background:transparent;color:var(--dim);
  display:grid;place-items:center;cursor:pointer;transition:background .16s var(--ease),color .16s var(--ease),transform .12s var(--ease)}
#fr-refresh svg{width:18px;height:18px}
#fr-refresh:hover{background:rgba(255,255,255,.09);color:#fff}
#fr-refresh:active{transform:scale(.9)}
#fr-refresh.loading{pointer-events:none;color:var(--accent-hi)}
#fr-refresh.loading svg{animation:refresh-spin .8s linear infinite}
@keyframes refresh-spin{to{transform:rotate(360deg)}}
#fr-online{display:flex;align-items:center;gap:9px;margin-top:12px;color:#7ddba0;font-size:15px}
#fr-online .dot{width:9px;height:9px;border-radius:50%;background:#5cd28a;animation:ping 2s var(--ease) infinite}
@keyframes ping{0%{box-shadow:0 0 0 0 rgba(92,210,138,.55)}70%{box-shadow:0 0 0 9px rgba(92,210,138,0)}100%{box-shadow:0 0 0 0 rgba(92,210,138,0)}}
#fr-hint{color:var(--muted);font-size:13px;margin-top:8px}
#fr-list{flex:1;min-height:0;overflow-y:auto;overscroll-behavior:contain;padding:10px 12px 16px}
#fr-list::-webkit-scrollbar{width:9px}
#fr-list::-webkit-scrollbar-thumb{background:rgba(255,255,255,.13);border-radius:9px;border:3px solid transparent;background-clip:content-box}
#fr-list::-webkit-scrollbar-thumb:hover{background:rgba(255,255,255,.24);background-clip:content-box}

.fr{display:flex;align-items:center;gap:13px;padding:11px 12px;border-radius:13px;position:relative;
  animation:rowin .34s var(--ease) both;transition:background .15s var(--ease),transform .15s var(--ease)}
.fr .st{flex:0 0 auto;width:10px;height:10px;border-radius:50%;background:#5a5c66}
.fr.on .st{background:#5cd28a;box-shadow:0 0 9px rgba(92,210,138,.75)}
.fr .txt{min-width:0;flex:1}
.fr .nm{display:block;font-weight:600;font-size:15px;color:var(--dim);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.fr.on .nm{color:#fff}
.fr .sv{display:block;color:var(--muted);font-size:13px;margin-top:3px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.fr .go{flex:0 0 auto;opacity:0;color:var(--accent);font-size:12px;font-weight:700;letter-spacing:.05em;transform:translateX(-6px);
  transition:opacity .18s var(--ease),transform .18s var(--ease)}
.fr.on{cursor:pointer}
.fr.on:hover{background:var(--row-hover);transform:translateX(3px)}
.fr.on:hover .go{opacity:1;transform:none}
.fr.on::before{content:"";position:absolute;left:0;top:50%;width:3px;height:0;border-radius:2px;background:var(--accent);
  transform:translateY(-50%);transition:height .2s var(--ease)}
.fr.on:hover::before{height:26px}
@keyframes rowin{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:none}}

.empty{padding:22px 14px;color:var(--muted);font-size:14px}
.empty b{display:block;color:var(--dim);font-size:15px;font-weight:600;margin-bottom:6px}
.sk{display:flex;align-items:center;gap:13px;padding:13px 12px}
.sk i,.sk u{display:block;text-decoration:none;border-radius:7px;
  background:linear-gradient(90deg,rgba(255,255,255,.05),rgba(255,255,255,.12),rgba(255,255,255,.05));
  background-size:220% 100%;animation:shim 1.3s linear infinite}
.sk i{width:10px;height:10px;border-radius:50%;flex:0 0 auto}
.sk u{height:11px}
.sk .c{flex:1}
.sk .c u:first-child{width:46%}
.sk .c u:last-child{width:72%;height:9px;margin-top:7px;opacity:.6}
@keyframes shim{to{background-position:-220% 0}}

@keyframes pop{from{opacity:0;transform:translateY(14px) scale(.985)}to{opacity:1;transform:none}}
@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style>
</head>
<body>
<div id="bg"><i class="b1"></i><i class="b2"></i><i class="b3"></i><i class="grain"></i></div>

<div id="titlebar">
  <div id="drag"></div>
  <button class="capbtn" id="btn-min" title="Minimize">
    <svg width="12" height="12" viewBox="0 0 12 12"><path d="M1 6h10" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
  </button>
  <button class="capbtn" id="btn-close" title="Close">
    <svg width="12" height="12" viewBox="0 0 12 12"><path d="M1.5 1.5l9 9M10.5 1.5l-9 9" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
  </button>
</div>

<div id="shell">
  <aside id="rail">
    <img id="mascot" alt="">
    <div class="spacer"></div>
    <button id="btn-gear" title="Settings">
      <svg width="22" height="22" viewBox="0 0 24 24" fill="none">
        <path d="M12 15.4a3.4 3.4 0 100-6.8 3.4 3.4 0 000 6.8z" stroke="currentColor" stroke-width="1.7"/>
        <path d="M19.5 12c0-.5-.05-1-.13-1.47l1.9-1.35-1.9-3.29-2.2.85c-.74-.6-1.6-1.06-2.53-1.33L14.3 3h-3.8l-.34 2.41c-.93.27-1.79.73-2.53 1.33l-2.2-.85-1.9 3.29 1.9 1.35a8.7 8.7 0 000 2.94l-1.9 1.35 1.9 3.29 2.2-.85c.74.6 1.6 1.06 2.53 1.33L10.5 21h3.8l.34-2.41a7.7 7.7 0 002.53-1.33l2.2.85 1.9-3.29-1.9-1.35c.08-.47.13-.97.13-1.47z"
          stroke="currentColor" stroke-width="1.7" stroke-linejoin="round"/>
      </svg>
    </button>
  </aside>

  <main id="main">
    <nav id="tabs">
      <span id="tabpill"></span>
      <button class="tab on" data-view="overview">Overview</button>
      <button class="tab" data-view="updates">Updates</button>
    </nav>

    <section class="view on" id="v-overview">
      <img id="logo" alt="">
      <div id="logofb">UClient</div>
      <div id="ov-meta">
        <div id="ov-status"></div>
      </div>
    </section>

    <section class="view" id="v-updates">
      <h1 id="up-title">Updates</h1>
      <div id="up-version"></div>
      <div id="up-state">Checking for updates...</div>
      <div id="up-status"></div>
      <div class="bar" id="up-bar"><span style="width:0%"></span></div>
    </section>

    <div id="play-stack">
    <div id="alert-wrap">
      <div id="alert-strip" tabindex="0" role="note" aria-label="Notice">
        <span class="alert-ico" aria-hidden="true">
          <svg viewBox="0 0 20 20" fill="none" aria-hidden="true">
            <path class="ico-tri" fill="currentColor" stroke="currentColor" stroke-width="2.6" stroke-linejoin="round"
              d="M10 2.4 17.4 16.6H2.6Z"/>
            <rect class="ico-mark" x="8.7" y="6.4" width="2.6" height="6.4" rx="1.3"/>
            <circle class="ico-mark" cx="10" cy="14.85" r="1.35"/>
          </svg>
        </span>
        <span class="alert-title" id="alert-title"></span>
      </div>
      <div id="alert-flyout" aria-hidden="true">
        <div id="alert-flyout-box">
          <div id="alert-flyout-inner"></div>
        </div>
      </div>
    </div>
    <div id="play-wrap">
      <button id="play" disabled class="mode-checking">
        <span class="play-normal">
          <span class="play-ico" aria-hidden="true">
            <svg viewBox="0 0 24 24" width="32" height="32" fill="none" aria-hidden="true">
              <path fill="currentColor" d="M8.05 5.55a1.35 1.35 0 0 1 2.09-1.12l8.31 5.54a1.35 1.35 0 0 1 0 2.24l-8.31 5.54a1.35 1.35 0 0 1-2.09-1.12V5.55z"/>
            </svg>
          </span>
          <span id="play-label">Play</span>
        </span>
        <span class="play-update">
          <span class="play-ico" aria-hidden="true">
            <svg viewBox="0 0 24 24" width="32" height="32" fill="none" aria-hidden="true">
              <path stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" d="M12 4v9"/>
              <path stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" d="M8.5 9.5 12 13l3.5-3.5"/>
              <path stroke="currentColor" stroke-width="2.4" stroke-linecap="round" d="M5 19h14"/>
            </svg>
          </span>
          <span id="update-label">Update</span>
        </span>
        <span class="play-checking">
          <div class="pbar-top indet"><span></span></div>
          <span class="play-check-label" id="play-check-label">Checking for updates</span>
        </span>
        <span class="play-progress">
          <div class="pbar-top"><span id="prog-fill"></span></div>
          <div class="prog-row">
            <span id="prog-size">0.0 / 0.0 GB</span>
            <span id="prog-speed">0 KB/s</span>
            <span id="prog-eta">--</span>
          </div>
          <span class="prog-label" id="prog-label">Downloading</span>
        </span>
        <span class="play-running"><span id="play-running-label">RUNNING</span></span>
      </button>
      <div id="play-hint-flyout" aria-hidden="true">
        <div id="play-hint-flyout-box">
          <div id="play-hint-flyout-inner"></div>
        </div>
      </div>
    </div>
    <div id="play-version"></div>
    </div>
  </main>

  <aside id="friends">
    <div id="fr-head">
      <div id="fr-title">
        <h2>Friends</h2>
        <button id="fr-refresh" type="button" title="Refresh friends" aria-label="Refresh friends">
          <svg viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path d="M20 11a8 8 0 1 0-2.34 5.66" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
            <path d="M20 5v6h-6" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
          </svg>
        </button>
      </div>
      <div id="fr-online"><span class="dot"></span><span id="fr-count">0 online</span></div>
      <div id="fr-hint">Double-click a friend to join their server</div>
    </div>
    <div id="fr-list"></div>
  </aside>
</div>

<div class="modal" id="settings-modal">
  <div class="modal-dim" id="settings-dim"></div>
  <div class="modal-box">
    <aside class="modal-side">
      <h2>Settings</h2>
      <nav class="modal-nav">
        <button class="on" type="button">General</button>
      </nav>
    </aside>
    <div class="modal-body">
      <div class="modal-head">
        <h3>General</h3>
        <button class="modal-x" id="settings-close" type="button" title="Close">
          <svg width="14" height="14" viewBox="0 0 12 12"><path d="M1.5 1.5l9 9M10.5 1.5l-9 9" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
        </button>
      </div>
      <div class="sec">Launch options</div>
      <div class="opt" id="opt-auto">
        <span class="sw"></span>
        <span><b>Launch game automatically</b>
        <small>Start UClient as soon as the update check finishes.</small></span>
      </div>
      <div class="opt" id="opt-auto-update">
        <span class="sw"></span>
        <span><b>Install updates automatically</b>
        <small>On launcher startup only. Mid-session updates stay manual.</small></span>
      </div>
      <div class="sec" style="margin-top:22px">Integrations</div>
      <div class="opt" id="opt-discord">
        <span class="sw"></span>
        <span><b>Show Discord activity</b>
        <small>Display your in-game status in Discord. Restart the client to apply.</small></span>
      </div>
    </div>
  </div>
</div>

<div id="dev-panel" class="collapsed">
  <div id="dev-head">
    <span class="dev-grip" aria-hidden="true">::</span>
    <span class="dev-title">Developer Tools</span>
    <small>DevRelease</small>
    <button type="button" id="dev-collapse" title="Collapse / expand">&#8722;</button>
  </div>
  <div id="dev-body">
    <div class="dev-row"><label><input type="checkbox" id="dev-force-update"> Force Update button</label></div>
    <div class="dev-row"><label><input type="checkbox" id="dev-force-blocked"> Force Play blocked</label></div>
    <div class="dev-row"><label><input type="checkbox" id="dev-force-game"> Force game running</label></div>
    <div class="dev-row"><label><input type="checkbox" id="dev-inject-notice"> Inject test notice</label></div>
    <div class="dev-actions">
      <button type="button" id="dev-fake-download">Fake download</button>
      <button type="button" id="dev-reset" class="warn">Reset</button>
    </div>
  </div>
</div>

<script>
"use strict";
var $ = function (id) { return document.getElementById(id); };
var send = function (o) { try { window.chrome.webview.postMessage(JSON.stringify(o)); } catch (e) {} };

/* -- window chrome ----------------------------------------------------- */
$("drag").addEventListener("mousedown", function (e) { if (e.button === 0) send({cmd: "drag"}); });
$("btn-min").addEventListener("click", function () { send({cmd: "minimize"}); });
$("btn-close").addEventListener("click", function () { send({cmd: "close"}); });
document.addEventListener("contextmenu", function (e) { e.preventDefault(); });
document.addEventListener("keydown", function (e) {
  if (e.key === "Escape") {
    if (settingsOpen) toggleSettings(false);
    else send({cmd: "close"});
  } else if (e.key === "Enter" && !$("play").disabled) {
    send({cmd: lastState.updateAvailable ? "update" : "play"});
  }
});

/* -- tabs -------------------------------------------------------------- */
var settingsOpen = false;
var noticeState = [];

function noticePriority(n) {
  var score = 0;
  if (n.blocksPlay) score += 100;
  if (n.severity === "critical") score += 30;
  else if (n.severity === "warning") score += 20;
  else score += 10;
  return score;
}

function sortedNotices(list) {
  return (list || []).slice().sort(function (a, b) {
    return noticePriority(b) - noticePriority(a);
  });
}

function fmtNoticeExpiry(n) {
  if (n.expiresAt === undefined) return "";
  if (n.expiresAt === null) return '<p class="notice-expiry"><b>Duration:</b> Permanent</p>';
  var ts = Number(n.expiresAt);
  if (!ts) return "";
  var d = new Date(ts * 1000);
  if (isNaN(d.getTime())) return "";
  return '<p class="notice-expiry"><b>Until:</b> ' + esc(d.toLocaleString()) + '</p>';
}

function renderAlerts(st) {
  noticeState = sortedNotices(st.notices || []);
  var wrap = $("alert-wrap");
  if (!noticeState.length) {
    wrap.classList.remove("on");
    return;
  }
  var top = noticeState[0];
  $("alert-title").textContent = top.title || "Notice";
  $("alert-strip").classList.remove("sev-critical", "sev-warning", "sev-info");
  $("alert-strip").classList.add("sev-" + (top.severity || "warning"));
  $("alert-flyout-inner").innerHTML = noticeState.map(function (n) {
    var expiry = n.id === "account_ban" ? fmtNoticeExpiry(n) : "";
    return '<article class="notice-block"><h4>' + esc(n.title || "Notice") +
      '</h4><p>' + esc(n.body || "") + '</p>' + expiry + '</article>';
  }).join("");
  wrap.classList.add("on");
}

function moveIndicator() {
  var btn = document.querySelector(".tab.on");
  if (!btn) return;
  var tabs = $("tabs").getBoundingClientRect();
  var r = btn.getBoundingClientRect();
  var pill = $("tabpill");
  pill.style.width = r.width + "px";
  pill.style.transform = "translateX(" + (r.left - tabs.left - 5) + "px)";
}

function showView(name) {
  var views = document.querySelectorAll(".view");
  for (var i = 0; i < views.length; i++) views[i].classList.remove("on");
  $("v-" + name).classList.add("on");
  var tabs = document.querySelectorAll(".tab");
  for (var k = 0; k < tabs.length; k++) tabs[k].classList.toggle("on", tabs[k].dataset.view === name);
  moveIndicator();
}

(function () {
  var tabs = document.querySelectorAll(".tab");
  for (var i = 0; i < tabs.length; i++) {
    tabs[i].addEventListener("click", function (e) {
      if (settingsOpen) toggleSettings(false);
      showView(e.currentTarget.dataset.view);
    });
  }
})();

function toggleSettings(on) {
  settingsOpen = on === undefined ? !settingsOpen : on;
  $("btn-gear").classList.toggle("on", settingsOpen);
  $("settings-modal").classList.toggle("on", settingsOpen);
}
$("btn-gear").addEventListener("click", function () { toggleSettings(); });
$("settings-close").addEventListener("click", function () { toggleSettings(false); });
$("settings-dim").addEventListener("click", function () { toggleSettings(false); });

/* -- dev panel --------------------------------------------------------- */
function sendDev(action, value) {
  var msg = {cmd: "dev", action: action};
  if (value !== undefined) msg.value = !!value;
  send(msg);
}
var devDrag = {active: false, ox: 0, oy: 0};
function saveDevPanelLayout() {
  var panel = $("dev-panel");
  if (!panel) return;
  try {
    localStorage.setItem("uclient.devPanel", JSON.stringify({
      left: parseFloat(panel.style.left) || panel.getBoundingClientRect().left,
      top: parseFloat(panel.style.top) || panel.getBoundingClientRect().top,
      collapsed: panel.classList.contains("collapsed")
    }));
  } catch (e) {}
}
function loadDevPanelLayout() {
  var panel = $("dev-panel");
  if (!panel) return;
  try {
    var raw = localStorage.getItem("uclient.devPanel");
    if (!raw) return;
    var s = JSON.parse(raw);
    if (typeof s.left === "number" && typeof s.top === "number") {
      panel.style.left = Math.max(8, s.left) + "px";
      panel.style.top = Math.max(8, s.top) + "px";
      panel.style.bottom = "auto";
    }
    panel.classList.toggle("collapsed", !!s.collapsed);
  } catch (e) {}
}
function clampDevPanelPos(panel, x, y) {
  var maxX = Math.max(8, window.innerWidth - panel.offsetWidth - 8);
  var maxY = Math.max(8, window.innerHeight - panel.offsetHeight - 8);
  panel.style.left = Math.max(8, Math.min(x, maxX)) + "px";
  panel.style.top = Math.max(8, Math.min(y, maxY)) + "px";
  panel.style.bottom = "auto";
}
function bindDevPanel() {
  var panel = $("dev-panel");
  if (!panel || panel.dataset.bound) return;
  panel.dataset.bound = "1";
  loadDevPanelLayout();
  $("dev-collapse").addEventListener("click", function (e) {
    e.stopPropagation();
    panel.classList.toggle("collapsed");
    saveDevPanelLayout();
  });
  $("dev-head").addEventListener("mousedown", function (e) {
    if (e.button !== 0) return;
    if (e.target.closest("#dev-collapse")) return;
    e.preventDefault();
    devDrag.active = true;
    var rect = panel.getBoundingClientRect();
    devDrag.ox = e.clientX - rect.left;
    devDrag.oy = e.clientY - rect.top;
    panel.style.bottom = "auto";
    clampDevPanelPos(panel, rect.left, rect.top);
  });
  document.addEventListener("mousemove", function (e) {
    if (!devDrag.active) return;
    clampDevPanelPos(panel, e.clientX - devDrag.ox, e.clientY - devDrag.oy);
  });
  document.addEventListener("mouseup", function () {
    if (!devDrag.active) return;
    devDrag.active = false;
    saveDevPanelLayout();
  });
  window.addEventListener("resize", function () {
    if (!panel || panel.style.top === "") return;
    var rect = panel.getBoundingClientRect();
    clampDevPanelPos(panel, rect.left, rect.top);
    saveDevPanelLayout();
  });
  $("dev-force-update").addEventListener("change", function (e) {
    sendDev("forceUpdate", e.target.checked);
  });
  $("dev-force-blocked").addEventListener("change", function (e) {
    sendDev("forcePlayBlocked", e.target.checked);
  });
  $("dev-force-game").addEventListener("change", function (e) {
    sendDev("forceGameRunning", e.target.checked);
  });
  $("dev-inject-notice").addEventListener("change", function (e) {
    sendDev("injectNotice", e.target.checked);
  });
  $("dev-fake-download").addEventListener("click", function () { sendDev("fakeDownload"); });
  $("dev-reset").addEventListener("click", function () { sendDev("reset"); });
}
function renderDev(st) {
  document.body.classList.toggle("dev-build", !!st.devBuild);
  if (!st.devBuild) return;
  bindDevPanel();
  $("dev-force-update").checked = !!st.devForceUpdate;
  $("dev-force-blocked").checked = !!st.devForcePlayBlocked;
  $("dev-force-game").checked = !!st.devForceGameRunning;
  $("dev-inject-notice").checked = !!st.devInjectNotice;
}

/* -- actions ----------------------------------------------------------- */
var lastState = {};
$("play").addEventListener("click", function () {
  if ($("play").disabled) return;
  send({cmd: lastState.updateAvailable ? "update" : "play"});
});
$("opt-auto").addEventListener("click", function () {
  var on = !$("opt-auto").classList.contains("on");
  $("opt-auto").classList.toggle("on", on);
  send({cmd: "autolaunch", value: on});
});
$("opt-auto-update").addEventListener("click", function () {
  var on = !$("opt-auto-update").classList.contains("on");
  $("opt-auto-update").classList.toggle("on", on);
  send({cmd: "autoupdate", value: on});
});
$("opt-discord").addEventListener("click", function () {
  var on = !$("opt-discord").classList.contains("on");
  $("opt-discord").classList.toggle("on", on);
  send({cmd: "discordRpc", value: on});
});
$("fr-refresh").addEventListener("click", function () {
  if ($("fr-refresh").classList.contains("loading")) return;
  send({cmd: "refreshFriends"});
});

/* -- friends ----------------------------------------------------------- */
var MIDDOT = " \u00b7 ";
var friendSig = "";
var artSig = "";

function esc(s) {
  return String(s == null ? "" : s).replace(/[&<>"']/g, function (c) {
    return {"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"}[c];
  });
}

function renderFriends(st) {
  var list = $("fr-list");
  var fr = st.friends || [];
  var initialLoading = st.friendsLoading && !st.friendsLoaded;
  var sig = initialLoading ? "initial-loading" : JSON.stringify([st.friendsLoaded, fr]);
  if (sig === friendSig) return;
  friendSig = sig;

  if (initialLoading) {
    var sk = "";
    for (var i = 0; i < 6; i++) sk += '<div class="sk"><i></i><div class="c"><u></u><u></u></div></div>';
    list.innerHTML = sk;
    return;
  }
  if (!fr.length) {
    list.innerHTML = '<div class="empty"><b>No friends yet</b>Add friends in the game client and they will show up here.</div>';
    return;
  }
  list.innerHTML = fr.map(function (f, i) {
    var sub = f.online
      ? (f.map && f.server ? f.map + MIDDOT + f.server : (f.map || f.server || "In a server"))
      : "Offline";
    return '<div class="fr' + (f.online ? " on" : "") + '" data-addr="' + esc(f.address || "") + '"' +
      ' style="animation-delay:' + Math.min(i * 26, 320) + 'ms">' +
      '<span class="st"></span>' +
      '<span class="txt"><span class="nm">' + esc(f.name) + '</span>' +
      '<span class="sv">' + esc(sub) + '</span></span>' +
      (f.online ? '<span class="go">JOIN</span>' : "") +
      '</div>';
  }).join("");
}

$("fr-list").addEventListener("dblclick", function (e) {
  var row = e.target.closest(".fr.on");
  if (!row) return;
  if (row.dataset.addr) send({cmd: "join", address: row.dataset.addr});
});

function setArt(st) {
  var sig = (st.logoUrl || "") + "|" + (st.mascotUrl || "");
  if (sig === artSig) return;
  artSig = sig;
  var logo = $("logo"), mascot = $("mascot");
  if (st.logoUrl) {
    logo.onerror = function () { logo.style.display = "none"; $("logofb").style.display = "block"; };
    logo.src = st.logoUrl;
  } else {
    logo.style.display = "none";
    $("logofb").style.display = "block";
  }
  if (st.mascotUrl) {
    mascot.onerror = function () { mascot.style.visibility = "hidden"; };
    mascot.src = st.mascotUrl;
  } else {
    mascot.style.visibility = "hidden";
  }
}

/* -- state ------------------------------------------------------------- */
function fmtBytes(n) {
  n = Number(n) || 0;
  if (n >= 1073741824) return (n / 1073741824).toFixed(1) + " GB";
  if (n >= 1048576) return (n / 1048576).toFixed(1) + " MB";
  if (n >= 1024) return Math.round(n / 1024) + " KB";
  return n + " B";
}
function fmtSpeed(bps) {
  bps = Number(bps) || 0;
  if (bps >= 1048576) return (bps / 1048576).toFixed(1) + " MB/s";
  if (bps >= 1024) return (bps / 1024).toFixed(1) + " KB/s";
  return "0 KB/s";
}
function fmtEta(sec) {
  sec = Number(sec);
  if (isNaN(sec) || sec < 0) return "--";
  if (sec < 60) return sec + "s";
  var m = Math.floor(sec / 60), s = sec % 60;
  if (m < 60) return m + "m " + s + "s";
  var h = Math.floor(m / 60);
  m = m % 60;
  return h + "h " + m + "m";
}

var playFaceModes = ["mode-play", "mode-update", "mode-checking", "mode-progress", "mode-running"];
function setPlayFaceMode(play, mode) {
  if (play.classList.contains("mode-update") && mode === "mode-progress") {
    play.classList.add("mode-progress");
    for (var i = 0; i < playFaceModes.length; i++) {
      if (playFaceModes[i] !== "mode-progress") play.classList.remove(playFaceModes[i]);
    }
    return;
  }
  if (play.classList.contains("mode-play") && mode === "mode-progress") {
    play.classList.add("mode-progress");
    for (var j = 0; j < playFaceModes.length; j++) {
      if (playFaceModes[j] !== "mode-progress") play.classList.remove(playFaceModes[j]);
    }
    return;
  }
  if (play.classList.contains("mode-progress") && (mode === "mode-update" || mode === "mode-play")) {
    play.classList.add(mode);
    for (var k = 0; k < playFaceModes.length; k++) {
      if (playFaceModes[k] !== mode) play.classList.remove(playFaceModes[k]);
    }
    return;
  }
  for (var n = 0; n < playFaceModes.length; n++) play.classList.remove(playFaceModes[n]);
  play.classList.add(mode);
}

function playHintRedundantWithAlert(st) {
  if (!st.playBlocked || !(st.notices && st.notices.length)) return false;
  for (var i = 0; i < st.notices.length; i++) {
    if (st.notices[i].blocksPlay) return true;
  }
  return false;
}

function playButtonShowsStatus(phase) {
  return phase === "checking" || phase === "launching" || phase === "updating";
}

window.__setState = function (st) {
  if (!st) return;
  lastState = st;
  setArt(st);
  renderDev(st);

  var play = $("play");
  var actionBlocked = !!st.playBlocked || (!!st.updateAvailable && !!st.gameRunning);
  play.disabled = st.phase !== "ready" || actionBlocked;

  if (st.phase === "ready") {
    if (st.updateAvailable) {
      setPlayFaceMode(play, "mode-update");
      $("update-label").textContent = st.buttonLabel || "Update";
    } else {
      setPlayFaceMode(play, "mode-play");
      $("play-label").textContent = st.buttonLabel || "Play";
    }
  } else if (st.phase === "checking") {
    setPlayFaceMode(play, "mode-checking");
    $("play-check-label").textContent = st.buttonLabel || "Checking for updates";
  } else if (st.phase === "launching") {
    setPlayFaceMode(play, "mode-running");
    $("play-running-label").textContent = st.buttonLabel || "RUNNING";
  } else if (st.phase === "updating") {
    setPlayFaceMode(play, "mode-progress");
    var pct = st.percent || 0;
    $("prog-fill").style.width = pct + "%";
    if (st.updateStage === "download") {
      var done = Number(st.downloadDone) || 0;
      var total = Number(st.downloadTotal) || 0;
      if (total > 0) {
        $("prog-size").textContent = fmtBytes(done) + " / " + fmtBytes(total);
        $("prog-speed").textContent = fmtSpeed(st.downloadSpeed);
        $("prog-eta").textContent = fmtEta(st.etaSeconds);
      } else {
        $("prog-size").textContent = fmtBytes(done);
        $("prog-speed").textContent = fmtSpeed(st.downloadSpeed);
        $("prog-eta").textContent = fmtEta(st.etaSeconds);
      }
      $("prog-label").textContent = st.buttonLabel || "Downloading update";
    } else {
      $("prog-size").textContent = pct + "%";
      $("prog-speed").textContent = st.buttonLabel || "Applying update";
      $("prog-eta").textContent = "";
      $("prog-label").textContent = st.status || "Installing files";
    }
  } else {
    setPlayFaceMode(play, "mode-checking");
    $("play-check-label").textContent = st.buttonLabel || "Please wait";
  }

  $("play-version").textContent = st.version || "";
  var playWrap = $("play-wrap");
  var showHint = st.phase === "ready" && play.disabled && !!st.buttonHint && !playHintRedundantWithAlert(st);
  playWrap.classList.toggle("hint-on", showHint);
  playWrap.tabIndex = showHint ? 0 : -1;
  $("play-hint-flyout-inner").textContent = showHint ? st.buttonHint : "";
  renderAlerts(st);
  $("ov-status").textContent = playButtonShowsStatus(st.phase) || st.phase === "ready" ? "" : (st.status || "");
  $("ov-status").classList.toggle("bad", !!st.failed && st.phase !== "ready");

  $("up-version").textContent = st.version || "";
  $("up-status").textContent = st.phase === "checking" || st.phase === "launching" ? "" : (st.status || "");
  $("up-status").classList.toggle("bad", !!st.failed);
  $("up-state").textContent =
    st.phase === "ready" ? (st.updateAvailable ? "An update is available." :
      (st.failed ? "Update check had a problem \u2014 you can still play." : "You are up to date.")) :
    st.phase === "updating" ? "Downloading and applying the update..." :
    st.phase === "launching" ? "Launching game..." : "Checking for updates...";

  var busy = st.phase === "checking" || st.phase === "updating" || st.phase === "launching";
  var bar = $("up-bar");
  bar.classList.toggle("on", busy);
  bar.classList.toggle("indet", st.phase === "checking");
  if (st.phase === "updating") bar.firstElementChild.style.width = (st.percent || 0) + "%";

  $("opt-auto").classList.toggle("on", !!st.autoLaunch);
  $("opt-auto-update").classList.toggle("on", !!st.autoUpdate);
  $("opt-discord").classList.toggle("on", !!st.discordRpc);
  $("fr-refresh").classList.toggle("loading", !!st.friendsLoading);

  var online = 0, all = st.friends || [];
  for (var i = 0; i < all.length; i++) if (all[i].online) online++;
  $("fr-count").textContent = online + " online";
  renderFriends(st);
};

window.addEventListener("resize", moveIndicator);
requestAnimationFrame(function () { moveIndicator(); send({cmd: "ready"}); });
</script>
</body>
</html>
)HTMLDOC";
