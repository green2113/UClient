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
//               shortcuts[] {id, name, enabled, trigger, actions[]},
//               updateStage, downloadDone, downloadTotal, downloadSpeed, etaSeconds,
//               friends[] {name, clan, online, afk, server, map, address}
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
.rail-btn,#btn-gear{
  width:44px;height:44px;border:0;background:transparent;border-radius:13px;color:var(--muted);
  display:grid;place-items:center;cursor:pointer;position:relative;
  transition:background .18s var(--ease),color .18s var(--ease);
}
.rail-btn{margin-top:8px}
.rail-btn svg,#btn-gear svg{transition:transform .5s var(--ease)}
.rail-btn:hover,#btn-gear:hover{background:rgba(255,255,255,.07);color:#fff}
.rail-btn.on,#btn-gear.on{color:#fff;background:rgba(124,108,240,.16)}
.rail-btn.on::before,#btn-gear.on::before{content:"";position:absolute;left:-14px;top:11px;width:3px;height:22px;border-radius:2px;background:var(--accent)}
#btn-gear:hover svg{transform:rotate(60deg)}
#rail-nav{display:flex;flex-direction:column;align-items:center;gap:4px;margin-top:18px}
#shell.shortcuts-mode #main,#shell.shortcuts-mode #friends{display:none}
#shortcuts-view{display:none;grid-column:2/4;min-width:0;padding:56px 34px 34px 44px;flex-direction:column;background:linear-gradient(160deg,#12141c 0%,#08090d 100%)}
#shell.shortcuts-mode #shortcuts-view{display:flex}
.sc-head{display:flex;align-items:center;justify-content:space-between;margin-bottom:18px}
.sc-head h1{font:800 34px/1 inherit;letter-spacing:-.5px}
.sc-section{color:#8e8e93;font-size:13px;font-weight:600;text-transform:uppercase;letter-spacing:.06em;margin:8px 0 12px}
.sc-card{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08);border-radius:16px;overflow:hidden}
.sc-row{display:grid;grid-template-columns:auto 1fr auto;gap:14px;align-items:center;padding:14px 16px;border-bottom:1px solid rgba(255,255,255,.06);cursor:pointer;transition:background .15s var(--ease)}
.sc-row:last-child{border-bottom:0}
.sc-row:hover{background:rgba(255,255,255,.04)}
.sc-row.off{opacity:.55}
.sc-flow{display:flex;align-items:center;gap:6px}
.sc-ico{width:28px;height:28px;border-radius:8px;display:grid;place-items:center;font-size:14px;background:rgba(255,255,255,.08)}
.sc-ico.chat{background:rgba(52,199,89,.18);color:#34c759}
.sc-ico.action{background:rgba(124,108,240,.22);color:#b8afff}
.sc-arrow{color:var(--muted);font-size:12px}
.sc-text b{display:block;font-size:15px;font-weight:600}
.sc-text small{display:block;color:var(--muted);font-size:12px;margin-top:3px}
.sc-sw{width:46px;height:28px;border-radius:99px;background:rgba(255,255,255,.14);position:relative;flex:0 0 auto;cursor:pointer;transition:background .22s var(--ease)}
.sc-sw::after{content:"";position:absolute;top:3px;left:3px;width:22px;height:22px;border-radius:50%;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.35);transition:transform .22s var(--ease)}
.sc-sw.on{background:#34c759}
.sc-sw.on::after{transform:translateX(18px)}
.sc-empty{padding:36px 20px;text-align:center;color:var(--muted)}
.sc-empty b{display:block;color:var(--text);font-size:17px;margin-bottom:8px}
.sc-fab{position:fixed;right:calc(372px + 34px);bottom:34px;width:56px;height:56px;border:0;border-radius:50%;background:var(--accent);color:#fff;font-size:28px;line-height:1;cursor:pointer;box-shadow:0 10px 28px rgba(124,108,240,.45);z-index:25;transition:transform .15s var(--ease),background .15s var(--ease);display:none}
.sc-fab:hover{transform:scale(1.05);background:var(--accent-hi)}
#shell.shortcuts-mode .sc-fab{display:block}
#sc-editor{position:fixed;inset:0;z-index:320;display:none;flex-direction:column;background:#f2f2f7;color:#111}
#sc-editor.on{display:flex}
.sc-ed-head{display:flex;align-items:center;gap:12px;padding:14px 18px;background:#fff;border-bottom:1px solid rgba(0,0,0,.08)}
.sc-ed-back{width:36px;height:36px;border:0;border-radius:10px;background:transparent;cursor:pointer;color:#007aff;font-size:22px;line-height:1}
.sc-ed-title{flex:1;border:0;background:transparent;font:700 18px/1 inherit;color:#111;outline:none}
.sc-ed-save{border:0;border-radius:999px;background:#007aff;color:#fff;font:600 14px/1 inherit;padding:10px 18px;cursor:pointer}
.sc-ed-canvas{flex:1;overflow:auto;padding:18px 18px 240px}
.sc-block{background:#fff;border-radius:14px;padding:14px 16px;margin-bottom:10px;box-shadow:0 1px 2px rgba(0,0,0,.06);position:relative}
.sc-block .sc-del{position:absolute;top:10px;right:10px;width:24px;height:24px;border:0;border-radius:6px;background:rgba(0,0,0,.05);color:#888;cursor:pointer;font-size:14px;line-height:1}
.sc-block-line{font-size:15px;line-height:1.7;color:#111}
.sc-pill{display:inline-block;border:0;border-radius:8px;background:rgba(0,122,255,.12);color:#007aff;font:600 14px/1.2 inherit;padding:4px 10px;margin:0 2px;cursor:pointer}
.sc-pill.input{min-width:40px;text-align:center}
.sc-drawer{position:absolute;left:0;right:0;bottom:0;height:min(52vh,460px);background:#fff;border-radius:18px 18px 0 0;box-shadow:0 -8px 30px rgba(0,0,0,.12);display:flex;flex-direction:column;transform:translateY(55%);transition:transform .28s var(--ease)}
.sc-drawer.expanded{transform:translateY(0)}
.sc-drawer-handle{width:40px;height:5px;border-radius:99px;background:rgba(0,0,0,.15);margin:10px auto 8px;cursor:pointer}
.sc-drawer-search{margin:0 16px 10px;padding:12px 14px;border-radius:12px;border:0;background:#f2f2f7;font:15px/1 inherit;outline:none}
.sc-drawer-chips{display:flex;gap:8px;padding:0 16px 10px;overflow:auto}
.sc-chip{border:0;border-radius:999px;background:#f2f2f7;color:#007aff;font:600 13px/1 inherit;padding:8px 14px;cursor:pointer;white-space:nowrap}
.sc-chip.on{background:rgba(0,122,255,.12)}
.sc-drawer-list{flex:1;overflow:auto;padding:0 16px 16px}
.sc-catalog{display:flex;align-items:center;gap:12px;padding:12px;border-radius:12px;cursor:pointer;transition:background .15s var(--ease)}
.sc-catalog:hover{background:#f2f2f7}
.sc-catalog-ico{width:32px;height:32px;border-radius:8px;display:grid;place-items:center;background:#f2f2f7;font-size:16px}
.sc-pop{position:fixed;z-index:400;background:#fff;border-radius:12px;box-shadow:0 12px 40px rgba(0,0,0,.18);padding:6px;min-width:160px;max-height:240px;overflow:auto}
.sc-pop button{display:block;width:100%;border:0;background:transparent;text-align:left;padding:10px 12px;border-radius:8px;font:15px/1 inherit;cursor:pointer;color:#111}
.sc-pop button:hover{background:#f2f2f7}
.sc-toast{position:fixed;left:50%;bottom:90px;transform:translateX(-50%);background:rgba(0,0,0,.82);color:#fff;padding:10px 16px;border-radius:999px;font-size:13px;opacity:0;pointer-events:none;transition:opacity .2s var(--ease);z-index:360}
.sc-toast.on{opacity:1}

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
.update-actions{margin-top:20px}
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
  position:relative;z-index:1;width:min(1040px,94vw);height:min(680px,88vh);
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
.modal-body{padding:28px 32px 24px;display:flex;flex-direction:column;min-width:0;overflow:hidden}
.modal-head{display:flex;align-items:center;justify-content:space-between;margin-bottom:28px}
.modal-head h3{font:700 22px/1 inherit}
.settings-pane{display:none;flex:1;min-height:0;overflow-y:auto;padding-right:6px}
.settings-pane.on{display:block}
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
.fr.on.afk .st{background:#e8b94b;box-shadow:0 0 9px rgba(232,185,75,.72)}
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

/* -- account onboarding and backups ----------------------------------- */
.account-view{overflow-y:auto;padding-right:8px}
.page-title{font:800 36px/1.1 inherit;letter-spacing:-.5px}
.page-sub{color:var(--dim);margin-top:10px}
.card{margin-top:24px;padding:22px;border:1px solid var(--line);border-radius:16px;background:rgba(20,22,28,.7)}
.form{display:flex;flex-direction:column;gap:12px;max-width:470px}
.form label{color:var(--dim);font-size:13px}
.field{width:100%;margin-top:5px;border:1px solid rgba(255,255,255,.13);border-radius:10px;background:rgba(0,0,0,.25);
  color:#fff;padding:11px 13px;font:inherit;user-select:text;outline:none}
.field:focus{border-color:var(--accent)}
.primary,.secondary,.danger{border:0;border-radius:10px;padding:11px 16px;color:#fff;font:650 14px/1 inherit;cursor:pointer}
.primary{background:var(--accent)}.secondary{background:rgba(255,255,255,.09)}.danger{background:#8f3434}
#account-logout{width:100%;height:34px;box-sizing:border-box;display:flex;align-items:center;gap:10px;padding:10px 14px;border-radius:10px;background:transparent;color:#d88a8a;border:0;text-align:left;font:600 14px/1 inherit;transition:background .16s var(--ease),color .16s var(--ease)}
#account-logout svg{width:14px;height:14px;flex:0 0 auto}
#account-logout:hover:not(:disabled){background:rgba(190,76,76,.18);color:#efaaaa}
#account-logout:disabled{color:var(--muted);opacity:.45;cursor:default}
.primary:disabled,.secondary:disabled,.danger:disabled{opacity:.45;cursor:default}
.row-actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:6px}
.fine{font-size:13px;color:var(--muted);line-height:1.5}.warn{color:#f2bd72}.err{color:#ff7a6e;min-height:20px}
#onboarding{position:fixed;inset:0;z-index:35;background:radial-gradient(circle at 70% 10%,#252047,#08090d 58%);
  display:none;align-items:center;justify-content:center;padding:50px}
#onboarding.on{display:flex}
.onboard-box{width:min(720px,94vw);max-height:88vh;overflow-y:auto;padding:34px;border:1px solid rgba(255,255,255,.12);
  border-radius:20px;background:rgba(13,14,19,.94);box-shadow:0 28px 90px rgba(0,0,0,.8)}
.onboard-box h1{font:800 34px/1.15 inherit}.onboard-box h2{font:700 23px/1.2 inherit;margin-bottom:8px}
.choice-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:26px}
.choice{border:1px solid var(--line);border-radius:14px;padding:22px;background:rgba(255,255,255,.04);color:#fff;text-align:left;cursor:pointer}
.choice:hover{border-color:var(--accent);background:rgba(124,108,240,.1)}.choice b{display:block;font-size:18px}.choice small{display:block;color:var(--dim);margin-top:8px}
.on-step{display:none}.on-step.on{display:block}.on-back{margin-bottom:18px}
.backup-toolbar{position:relative;display:flex;justify-content:space-between;align-items:center;gap:12px}.usage{color:var(--dim);font-size:13px}
.backup-list{display:flex;flex-direction:column;gap:8px;margin-top:14px;max-height:205px;overflow:auto;overscroll-behavior-y:contain;padding-right:4px}
.backup-row{display:flex;align-items:center;gap:11px;padding:11px 12px;border:1px solid var(--line);border-radius:10px;background:rgba(255,255,255,.025)}
.backup-row .grow{flex:1;min-width:0}.backup-row b{display:block;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.backup-row small{color:var(--muted)}
.backup-row input[type="checkbox"]{
  -webkit-appearance:none;appearance:none;flex:0 0 auto;width:18px;height:18px;margin:0;
  display:grid;place-content:center;border:1px solid rgba(255,255,255,.26);border-radius:5px;
  background:rgba(0,0,0,.2);cursor:pointer;
  transition:background .16s var(--ease),border-color .16s var(--ease),box-shadow .16s var(--ease);
}
.backup-row input[type="checkbox"]::after{
  content:"";width:5px;height:9px;border:solid #fff;border-width:0 2px 2px 0;
  transform:translateY(-1px) rotate(45deg) scale(0);transition:transform .14s var(--ease);
}
.backup-row input[type="checkbox"]:hover:not(:disabled){border-color:var(--accent-hi);background:rgba(124,108,240,.12)}
.backup-row input[type="checkbox"]:checked{
  border-color:var(--accent);background:var(--accent);box-shadow:0 0 0 3px rgba(124,108,240,.14);
}
.backup-row input[type="checkbox"]:checked::after{transform:translateY(-1px) rotate(45deg) scale(1)}
.backup-row input[type="checkbox"]:focus-visible{outline:2px solid rgba(155,142,248,.8);outline-offset:2px}
.backup-row input[type="checkbox"]:disabled{opacity:.38;cursor:default}
.backup-panel-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:22px}
.backup-tabs{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.backup-tabs{padding:4px;border:1px solid var(--line);border-radius:11px;background:rgba(0,0,0,.18)}
.backup-tabs button{border:0;border-radius:8px;color:var(--dim);background:transparent;font:650 13px/1 inherit;cursor:pointer;padding:9px 18px}
.backup-tabs button.on{color:#fff;background:rgba(124,108,240,.24)}
.backup-view{display:none}.backup-view.on{display:block}
.backup-toolbar-actions{display:flex;align-items:center;gap:9px}
.backup-filter-button{position:relative;width:34px;height:34px;padding:0;border:1px solid var(--line);border-radius:9px;background:rgba(255,255,255,.045);color:var(--dim);display:grid;place-items:center;cursor:pointer}
.backup-filter-button:hover,.backup-filter-button.on{color:#fff;border-color:rgba(124,108,240,.6);background:rgba(124,108,240,.15)}
.backup-filter-button svg{width:16px;height:16px}
.backup-filter-button.filtered::after{content:"";position:absolute;right:5px;top:5px;width:6px;height:6px;border-radius:50%;background:var(--accent-hi);box-shadow:0 0 0 2px #191b22}
.backup-filter-menu{position:absolute;right:0;top:42px;z-index:30;width:235px;max-height:300px;overflow:auto;padding:8px;border:1px solid rgba(255,255,255,.12);border-radius:11px;background:#202229;box-shadow:0 14px 38px rgba(0,0,0,.48);display:none}
.backup-filter-menu.on{display:block}
.backup-filter-title{padding:7px 9px 5px;color:var(--muted);font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.06em}
.backup-filter-title:not(:first-child){margin-top:6px;padding-top:10px;border-top:1px solid var(--line)}
.backup-filter-option{width:100%;min-height:32px;display:flex;align-items:center;justify-content:space-between;gap:8px;padding:8px 9px;border:0;border-radius:7px;background:transparent;color:var(--dim);font:600 13px/1.2 inherit;text-align:left;cursor:pointer}
.backup-filter-option+.backup-filter-option{margin-top:3px}
.backup-filter-option:hover{color:#fff;background:rgba(255,255,255,.06)}
.backup-filter-option.on{color:#fff;background:rgba(124,108,240,.18)}
.backup-filter-option::after{content:"";display:block;flex:0 0 14px;width:14px;height:14px;line-height:14px;text-align:center}
.backup-filter-option.on::after{content:"\2713";color:var(--accent-hi)}
.backup-date-group{margin-top:15px}
.backup-date-toggle{width:100%;display:flex;align-items:center;gap:7px;margin:0 0 7px;padding:3px;border:0;background:transparent;color:var(--dim);font:700 12px/1 inherit;letter-spacing:.04em;text-align:left;cursor:pointer}
.backup-date-toggle::before{content:"";width:6px;height:6px;border-right:1.5px solid currentColor;border-bottom:1.5px solid currentColor;transform:rotate(45deg) translateY(-1px);transition:transform .14s var(--ease)}
.backup-date-group.collapsed .backup-date-toggle::before{transform:rotate(-45deg)}
.backup-date-group.collapsed>.backup-list{display:none}
.backup-date-group .backup-list{margin-top:0;max-height:none;overflow:visible;overscroll-behavior:auto}
#backup-server{max-height:285px;min-height:0;overflow-y:auto;overscroll-behavior-y:contain}
.backup-row.is-conflict{opacity:.62;cursor:not-allowed}
.backup-row.is-conflict input{pointer-events:none}
.backup-delete{flex:0 0 auto;width:32px;height:32px;padding:0;display:grid;place-items:center;border:1px solid rgba(216,96,96,.62);background:transparent;color:#d88a8a;transition:background .15s var(--ease),border-color .15s var(--ease),color .15s var(--ease)}
.backup-delete:hover:not(:disabled){border-color:#df7373;background:rgba(190,76,76,.24);color:#f2aaaa}
.backup-delete svg{width:15px;height:15px}
.settings-account-footer{position:relative;margin-top:10px;padding-top:10px;border-top:1px solid var(--line)}
.settings-version{margin-top:auto;padding:14px 14px 0;color:var(--muted);font-size:11px;line-height:1;text-align:left;letter-spacing:.03em}
.logout-tooltip{
  position:fixed;z-index:400;padding:6px 10px;border-radius:8px;
  background:#2b2e34;color:#e9e9ec;font-size:13px;font-weight:500;line-height:1.35;
  white-space:nowrap;pointer-events:none;
  box-shadow:inset 0 0 0 1px rgba(255,255,255,.1),0 4px 14px rgba(0,0,0,.3);
  animation:logout-tooltip-in 140ms cubic-bezier(.23,1,.32,1) both;
}
.logout-tooltip[data-side="top"]{--tip-lift:3px;transform-origin:var(--tip-arrow,50%) 100%}
.logout-tooltip[data-side="bottom"]{--tip-lift:-3px;transform-origin:var(--tip-arrow,50%) 0}
.logout-tooltip::after{
  content:"";position:absolute;left:var(--tip-arrow,50%);width:9px;height:9px;
  margin-left:-4.5px;background:#2b2e34;transform:rotate(45deg);
}
.logout-tooltip[data-side="top"]::after{
  bottom:-4px;
  border-right:1px solid rgba(255,255,255,.1);border-bottom:1px solid rgba(255,255,255,.1);
  border-bottom-right-radius:2.5px;
}
.logout-tooltip[data-side="bottom"]::after{
  top:-4px;border-top:1px solid rgba(255,255,255,.1);border-left:1px solid rgba(255,255,255,.1);
  border-top-left-radius:2.5px;
}
.logout-tooltip.is-out{animation:logout-tooltip-out 110ms cubic-bezier(.23,1,.32,1) both}
@keyframes logout-tooltip-in{
  from{opacity:0;transform:scale(.92) translateY(var(--tip-lift,3px))}
  to{opacity:1;transform:scale(1) translateY(0)}
}
@keyframes logout-tooltip-out{
  from{opacity:1;transform:scale(1) translateY(0)}
  to{opacity:0;transform:scale(.96) translateY(var(--tip-lift,3px))}
}
.confirm-layer{
  position:fixed;inset:0;z-index:500;display:flex;align-items:center;justify-content:center;
  padding:24px;opacity:0;visibility:hidden;pointer-events:none;
  transition:opacity .16s var(--ease),visibility .16s var(--ease);
}
.confirm-layer.on{opacity:1;visibility:visible;pointer-events:auto}
.confirm-dim{position:absolute;inset:0;background:rgba(0,0,0,.68);backdrop-filter:blur(7px)}
.confirm-box{
  position:relative;width:min(430px,calc(100vw - 48px));padding:22px;
  border:1px solid rgba(255,255,255,.11);border-radius:14px;
  background:linear-gradient(160deg,#20222b,#16181e);box-shadow:0 24px 70px rgba(0,0,0,.72);
  transform:translateY(8px) scale(.97);transition:transform .2s var(--ease);
}
.confirm-layer.on .confirm-box{transform:none}
.confirm-box h3{font:700 18px/1.25 inherit;color:#fff}
.confirm-box p{margin-top:9px;color:var(--dim);font-size:14px;line-height:1.5}
.confirm-files{display:flex;flex-direction:column;gap:5px;margin-top:13px;max-height:132px;overflow-y:auto;padding:9px 10px;border:1px solid var(--line);border-radius:9px;background:rgba(0,0,0,.18)}
.confirm-file{color:var(--dim);font-size:12px;line-height:1.35;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.confirm-file.more{color:var(--muted);font-style:italic}
.confirm-actions{display:flex;justify-content:flex-end;gap:9px;margin-top:22px}

:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list){
  scrollbar-width:thin;scrollbar-color:rgba(255,255,255,.2) transparent;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list)::-webkit-scrollbar{
  width:10px;height:10px;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list)::-webkit-scrollbar-track{
  background:transparent;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list)::-webkit-scrollbar-thumb{
  min-height:36px;border:3px solid transparent;border-radius:999px;
  background:rgba(255,255,255,.18);background-clip:padding-box;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list)::-webkit-scrollbar-thumb:hover{
  background:rgba(124,108,240,.62);background-clip:padding-box;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list)::-webkit-scrollbar-button{
  display:none;width:0;height:0;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list)::-webkit-scrollbar-corner{
  background:transparent;
}

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
    <div id="rail-nav">
      <button class="rail-btn on" id="btn-home" type="button" title="Home">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" aria-hidden="true">
          <path d="M4 10.5 12 4l8 6.5V20a1 1 0 0 1-1 1h-5v-6H10v6H5a1 1 0 0 1-1-1v-9.5z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/>
        </svg>
      </button>
      <button class="rail-btn" id="btn-shortcuts" type="button" title="Shortcuts">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" aria-hidden="true">
          <path d="M7 4h10a2 2 0 0 1 2 2v3H5V6a2 2 0 0 1 2-2zm-2 7h14v9a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-9z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/>
        </svg>
      </button>
    </div>
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
      <div class="update-actions"><button class="primary" id="update-now" type="button" hidden>Update now</button></div>
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

  <section id="shortcuts-view" aria-hidden="true">
    <div class="sc-head"><h1>Automation</h1></div>
    <div class="sc-section">Personal</div>
    <div class="sc-card" id="sc-list"></div>
    <button class="sc-fab" id="sc-fab" type="button" title="Create automation">+</button>
  </section>
</div>

<div id="sc-editor" aria-hidden="true">
  <div class="sc-ed-head">
    <button class="sc-ed-back" id="sc-ed-back" type="button" title="Back">&lsaquo;</button>
    <input class="sc-ed-title" id="sc-ed-title" type="text" placeholder="New Shortcut" maxlength="64">
    <button class="sc-ed-save" id="sc-ed-save" type="button">Save</button>
    <button class="sc-ed-save" id="sc-ed-delete" type="button" style="background:#ff3b30;display:none">Delete</button>
  </div>
  <div class="sc-ed-canvas" id="sc-ed-canvas"></div>
  <div class="sc-drawer" id="sc-drawer">
    <div class="sc-drawer-handle" id="sc-drawer-handle"></div>
    <input class="sc-drawer-search" id="sc-drawer-search" type="search" placeholder="Search actions">
    <div class="sc-drawer-chips" id="sc-drawer-chips"></div>
    <div class="sc-drawer-list" id="sc-drawer-list"></div>
  </div>
</div>
<div class="sc-toast" id="sc-toast">Saved</div>

<div id="onboarding">
  <div class="onboard-box">
    <div class="on-step on" id="on-choice">
      <h1>Welcome to UClient</h1>
      <p class="page-sub">Choose how you want to continue.</p>
      <div class="card" id="on-saved-account" style="display:none">
        <h3>Saved UUID account</h3>
        <p class="fine" style="margin:8px 0 4px">Continue with the UClient account saved on this computer.</p>
        <p class="fine" id="saved-account-id" style="margin:0 0 14px"></p>
        <button class="primary" id="login-saved-account" type="button">Continue with saved UUID</button>
      </div>
      <div class="choice-grid">
        <button class="choice" type="button" data-on-step="on-existing"><b>Existing UClient user</b><small>Sign in with email or an existing UUID and account key.</small></button>
        <button class="choice" type="button" data-on-step="on-new"><b>Create a new account</b><small>Register with email or create a limited anonymous account.</small></button>
      </div>
    </div>
    <div class="on-step" id="on-existing">
      <button class="secondary on-back" type="button">Back</button><h2>Existing UClient user</h2>
      <div class="card">
        <form class="form" id="login-email-form">
          <h3>Email sign in</h3>
          <label>Email<input class="field" id="login-email" type="email" required autocomplete="email"></label>
          <label>Password<input class="field" id="login-password" type="password" minlength="10" maxlength="128" required autocomplete="current-password"></label>
          <button class="primary" type="submit">Sign in</button>
        </form>
      </div>
      <div class="card">
        <form class="form" id="login-key-form">
          <h3>UUID and account key</h3>
          <label>Install UUID<input class="field" id="login-install-id" required autocomplete="off"></label>
          <label>Account key<input class="field" id="login-account-key" type="password" minlength="32" maxlength="128" required autocomplete="off"></label>
          <button class="secondary" type="submit">Use account key</button>
        </form>
      </div>
    </div>
    <div class="on-step" id="on-new">
      <button class="secondary on-back" type="button">Back</button><h2>Create a new account</h2>
      <div class="card">
        <form class="form" id="register-email-form">
          <h3>Register with email</h3>
          <label>Email<input class="field" id="register-email" type="email" required autocomplete="email"></label>
          <label>Password<input class="field" id="register-password" type="password" minlength="10" maxlength="128" required autocomplete="new-password"></label>
          <p class="fine">Email verification is not currently available, and lost passwords cannot be recovered.</p>
          <button class="primary" type="submit">Create account</button>
        </form>
      </div>
      <div class="card">
        <h3>Create without email</h3>
        <p class="fine warn">Creating an account without an email and password may prevent you from using some features.</p>
        <p class="fine" lang="ko">&#xC774;&#xBA54;&#xC77C;&#xACFC; &#xBE44;&#xBC00;&#xBC88;&#xD638; &#xC5C6;&#xC774; &#xACC4;&#xC815;&#xC744; &#xC0DD;&#xC131;&#xD558;&#xBA74; &#xC77C;&#xBD80; &#xAE30;&#xB2A5;&#xC744; &#xC0AC;&#xC6A9;&#xD558;&#xC9C0; &#xBABB;&#xD560; &#xC218; &#xC788;&#xC2B5;&#xB2C8;&#xB2E4;.</p>
        <button class="secondary" id="register-anonymous" type="button" style="margin-top:14px">Create anonymous account</button>
      </div>
    </div>
    <div class="on-step" id="on-wait"><h2 id="on-wait-title">Checking your account...</h2><p class="page-sub">Please wait.</p></div>
    <div class="err" id="on-error"></div>
  </div>
</div>

<div class="modal" id="settings-modal">
  <div class="modal-dim" id="settings-dim"></div>
  <div class="modal-box">
    <aside class="modal-side">
      <h2>Settings</h2>
      <nav class="modal-nav">
        <button class="on" type="button" data-settings-view="general">General</button>
        <button type="button" data-settings-view="backup">File Backup</button>
      </nav>
      <div class="settings-account-footer">
        <button class="danger" id="account-logout" type="button">
          <svg viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path d="M10 5H5v14h5M14 8l4 4-4 4M8 12h10" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
          </svg>
          <span>Logout</span>
        </button>
      </div>
      <div class="settings-version" id="settings-launcher-version"></div>
    </aside>
    <div class="modal-body">
      <div class="modal-head">
        <h3 id="settings-title">General</h3>
        <button class="modal-x" id="settings-close" type="button" title="Close">
          <svg width="14" height="14" viewBox="0 0 12 12"><path d="M1.5 1.5l9 9M10.5 1.5l-9 9" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
        </button>
      </div>
      <div class="settings-pane on" id="settings-general">
        <div class="sec">Launch options</div>
        <div class="opt" id="opt-auto">
          <span class="sw"></span>
          <span><b>Launch game automatically</b>
          <small>Start UClient as soon as the update check finishes.</small></span>
        </div>
        <div class="opt" id="opt-auto-update">
          <span class="sw"></span>
          <span><b>Install client updates automatically</b>
          <small>On launcher startup only. Mid-session updates stay manual.</small></span>
        </div>
        <div class="sec" style="margin-top:22px">Integrations</div>
        <div class="opt" id="opt-discord">
          <span class="sw"></span>
          <span><b>Show Discord activity</b>
          <small>Display your in-game status in Discord. Restart the client to apply.</small></span>
        </div>
      </div>
      <div class="settings-pane" id="settings-backup">
        <p class="page-sub">Keep versions of selected files from your DDNet folder in your UClient account.</p>
        <p class="fine">Allowed types: CFG, TXT, PNG, JPG, JPEG, and LOG. Total cloud storage is limited to 10 MB.</p>
        <div id="backup-link" class="card">
          <h3>Connect an email first</h3>
          <p class="fine" style="margin:8px 0 16px">Cloud backups require an email-connected account.</p>
          <form class="form" id="backup-link-form">
            <label>Email<input class="field" id="link-email" type="email" required autocomplete="email"></label>
            <label>Password<input class="field" id="link-password" type="password" minlength="10" maxlength="128" required autocomplete="new-password"></label>
            <button class="primary" type="submit">Connect email</button>
          </form>
        </div>
        <div id="backup-main" style="display:none">
          <div class="backup-panel-head">
            <div class="backup-tabs">
              <button class="on" type="button" data-backup-view="upload">Backup</button>
              <button type="button" data-backup-view="restore">Restore</button>
            </div>
            <button class="secondary" id="backup-refresh" type="button">Refresh</button>
          </div>
          <div class="backup-view on" id="backup-view-upload">
            <div class="card">
              <div class="backup-toolbar">
                <h3>Local files</h3>
                <div class="backup-toolbar-actions">
                  <span class="usage" id="backup-usage"></span>
                  <button class="backup-filter-button" id="backup-filter-button" type="button" aria-label="Filter files" aria-expanded="false">
                    <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M4 6h16M7 12h10M10 18h4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
                  </button>
                </div>
                <div class="backup-filter-menu" id="backup-filter-menu"></div>
              </div>
              <div class="backup-list" id="backup-local"></div>
              <div class="row-actions"><button class="primary" id="backup-upload" type="button">Upload</button></div>
            </div>
          </div>
          <div class="backup-view" id="backup-view-restore">
            <div class="card">
              <div class="backup-toolbar">
                <h3>Saved versions</h3>
                <div class="backup-toolbar-actions">
                  <span class="usage" id="backup-restore-count"></span>
                  <button class="backup-filter-button" id="restore-filter-button" type="button" aria-label="Filter saved versions" aria-expanded="false">
                    <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M4 6h16M7 12h10M10 18h4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
                  </button>
                </div>
                <div class="backup-filter-menu" id="restore-filter-menu"></div>
              </div>
              <div class="backup-list" id="backup-server"></div>
              <div class="row-actions"><button class="primary" id="backup-restore-selected" type="button">Restore</button></div>
            </div>
          </div>
        </div>
        <div class="err" id="backup-error"></div>
      </div>
    </div>
  </div>
</div>

<div class="logout-tooltip" id="logout-tooltip" role="tooltip" hidden>Close DDNet before logging out</div>

<div class="confirm-layer" id="confirm-layer" aria-hidden="true">
  <div class="confirm-dim" id="confirm-dim"></div>
  <div class="confirm-box" role="dialog" aria-modal="true" aria-labelledby="confirm-title" aria-describedby="confirm-message">
    <h3 id="confirm-title"></h3>
    <p id="confirm-message"></p>
    <div class="confirm-files" id="confirm-files" hidden></div>
    <div class="confirm-actions">
      <button class="secondary" id="confirm-cancel" type="button">Cancel</button>
      <button class="primary" id="confirm-accept" type="button">Confirm</button>
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
var confirmDialogOpen = false;
var confirmAction = null;

/* -- window chrome ----------------------------------------------------- */
$("drag").addEventListener("mousedown", function (e) { if (e.button === 0) send({cmd: "drag"}); });
$("btn-min").addEventListener("click", function () { send({cmd: "minimize"}); });
$("btn-close").addEventListener("click", function () { send({cmd: "close"}); });
document.addEventListener("contextmenu", function (e) { e.preventDefault(); });
document.addEventListener("keydown", function (e) {
  if (e.key === "Escape") {
    if (confirmDialogOpen) closeConfirmDialog(false);
    else if (backupFilterOpen || restoreFilterOpen) {
      setBackupFilterOpen(false);
      setRestoreFilterOpen(false);
    }
    else if (settingsOpen) toggleSettings(false);
  } else if (e.key === "Enter" && !e.target.closest("input,button,form") && !$("play").disabled) {
    send({cmd: "play"});
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
  if (!settingsOpen) {
    hideLogoutTooltip(true);
    setBackupFilterOpen(false);
    setRestoreFilterOpen(false);
  }
}
function showSettingsView(name) {
  var panes = document.querySelectorAll(".settings-pane");
  for (var i = 0; i < panes.length; i++) panes[i].classList.toggle("on", panes[i].id === "settings-" + name);
  var buttons = document.querySelectorAll(".modal-nav button[data-settings-view]");
  for (var j = 0; j < buttons.length; j++) buttons[j].classList.toggle("on", buttons[j].dataset.settingsView === name);
  $("settings-title").textContent = name === "backup" ? "File Backup" : "General";
}
document.querySelector(".modal-nav").addEventListener("click", function (e) {
  var button = e.target.closest("button[data-settings-view]");
  if (button) showSettingsView(button.dataset.settingsView);
});
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

function openConfirmDialog(title, message, acceptLabel, danger, action, details) {
  confirmAction = action;
  confirmDialogOpen = true;
  $("confirm-title").textContent = title;
  $("confirm-message").textContent = message;
  var files = $("confirm-files");
  files.innerHTML = "";
  var detailItems = Array.isArray(details) ? details : [];
  detailItems.slice(0, 5).forEach(function (detail) {
    var row = document.createElement("div");
    row.className = "confirm-file";
    row.textContent = detail;
    files.appendChild(row);
  });
  if (detailItems.length > 5) {
    var more = document.createElement("div");
    more.className = "confirm-file more";
    more.textContent = "... and " + (detailItems.length - 5) + " more";
    files.appendChild(more);
  }
  files.hidden = !detailItems.length;
  $("confirm-accept").textContent = acceptLabel || "Confirm";
  $("confirm-accept").className = danger ? "danger" : "primary";
  $("confirm-layer").classList.add("on");
  $("confirm-layer").setAttribute("aria-hidden", "false");
  $("confirm-cancel").focus();
}
function closeConfirmDialog(accepted) {
  if (!confirmDialogOpen) return;
  var action = confirmAction;
  confirmAction = null;
  confirmDialogOpen = false;
  $("confirm-layer").classList.remove("on");
  $("confirm-layer").setAttribute("aria-hidden", "true");
  if (accepted && action) action();
}
$("confirm-cancel").addEventListener("click", function () { closeConfirmDialog(false); });
$("confirm-dim").addEventListener("click", function () { closeConfirmDialog(false); });
$("confirm-accept").addEventListener("click", function () { closeConfirmDialog(true); });

/* -- actions ----------------------------------------------------------- */
var lastState = {};
var logoutTooltipExitTimer = 0;
var tooltipAnchor = null;
function positionLogoutTooltip() {
  var anchor = tooltipAnchor;
  var bubble = $("logout-tooltip");
  if (!anchor || !bubble || bubble.hidden) return;
  var from = anchor.getBoundingClientRect();
  var box = bubble.getBoundingClientRect();
  var gap = 8;
  var margin = 8;
  var above = from.top > box.height + gap;
  var centred = from.left + from.width / 2 - box.width / 2;
  var left = Math.min(Math.max(centred, margin), window.innerWidth - box.width - margin);
  bubble.dataset.side = above ? "top" : "bottom";
  bubble.style.left = left + "px";
  bubble.style.top = (above ? from.top - box.height - gap : from.bottom + gap) + "px";
  bubble.style.setProperty("--tip-arrow", (from.left + from.width / 2 - left) + "px");
}
function showActionTooltip(anchor, label) {
  if (!anchor) return;
  clearTimeout(logoutTooltipExitTimer);
  tooltipAnchor = anchor;
  var bubble = $("logout-tooltip");
  bubble.textContent = label;
  bubble.classList.remove("is-out");
  bubble.hidden = false;
  positionLogoutTooltip();
}
function showLogoutTooltip() {
  var button = $("account-logout");
  if (!button.parentElement.classList.contains("logout-blocked")) return;
  showActionTooltip(button, "Close DDNet before logging out");
}
function hideLogoutTooltip(immediate) {
  clearTimeout(logoutTooltipExitTimer);
  tooltipAnchor = null;
  var bubble = $("logout-tooltip");
  if (bubble.hidden) return;
  if (immediate) {
    bubble.hidden = true;
    bubble.classList.remove("is-out");
    return;
  }
  bubble.classList.add("is-out");
  logoutTooltipExitTimer = setTimeout(function () {
    bubble.hidden = true;
    bubble.classList.remove("is-out");
  }, 110);
}
var logoutFooter = $("account-logout").parentElement;
logoutFooter.addEventListener("pointerenter", showLogoutTooltip);
logoutFooter.addEventListener("pointerleave", function () { hideLogoutTooltip(false); });
document.addEventListener("pointerover", function (e) {
  var conflict = e.target instanceof Element ? e.target.closest("#backup-server .backup-row.is-conflict") : null;
  if (conflict && !e.target.closest("button")) {
    showActionTooltip(conflict, "The same file is already selected.");
    return;
  }
  var target = e.target instanceof Element ? e.target.closest("#settings-backup button:disabled") : null;
  if (target && lastState.gameRunning)
    showActionTooltip(target, "Close DDNet before backing up or restoring");
});
document.addEventListener("pointerout", function (e) {
  if (!tooltipAnchor || !tooltipAnchor.closest("#settings-backup")) return;
  var related = e.relatedTarget;
  if (related instanceof Node && tooltipAnchor.contains(related)) return;
  hideLogoutTooltip(false);
});
window.addEventListener("resize", function () { hideLogoutTooltip(true); });
window.addEventListener("scroll", function () { hideLogoutTooltip(true); }, true);
$("play").addEventListener("click", function () {
  if ($("play").disabled) return;
  send({cmd: "play"});
});
$("update-now").addEventListener("click", function () {
  if ($("update-now").disabled) return;
  send({cmd: "update"});
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

/* -- account and backup actions --------------------------------------- */
function setOnStep(id) {
  var steps = document.querySelectorAll(".on-step");
  for (var i = 0; i < steps.length; i++) steps[i].classList.toggle("on", steps[i].id === id);
  $("on-error").textContent = "";
}
document.querySelectorAll("[data-on-step]").forEach(function (b) {
  b.addEventListener("click", function () { setOnStep(b.dataset.onStep); });
});
document.querySelectorAll(".on-back").forEach(function (b) {
  b.addEventListener("click", function () { setOnStep("on-choice"); });
});
function sendPasswordForm(formId, cmd, emailId, passwordId) {
  $(formId).addEventListener("submit", function (e) {
    e.preventDefault();
    var password = $(passwordId).value;
    send({cmd: cmd, email: $(emailId).value, password: password});
    $(passwordId).value = "";
    password = "";
    setOnStep("on-wait");
  });
}
sendPasswordForm("login-email-form", "accountLoginEmail", "login-email", "login-password");
sendPasswordForm("register-email-form", "accountRegisterEmail", "register-email", "register-password");
$("login-key-form").addEventListener("submit", function (e) {
  e.preventDefault();
  var key = $("login-account-key").value;
  send({cmd: "accountLoginKey", installId: $("login-install-id").value, accountKey: key});
  $("login-account-key").value = "";
  key = "";
  setOnStep("on-wait");
});
$("register-anonymous").addEventListener("click", function () {
  openConfirmDialog(
    "Create without email?",
    "Accounts without an email cannot use every feature.",
    "Create account",
    false,
    function () {
      send({cmd: "accountRegisterAnonymous"});
      setOnStep("on-wait");
    }
  );
});
$("backup-link-form").addEventListener("submit", function (e) {
  e.preventDefault();
  var password = $("link-password").value;
  send({cmd: "accountLinkEmail", email: $("link-email").value, password: password});
  $("link-password").value = "";
  password = "";
});
$("account-logout").addEventListener("click", function () {
  if (lastState && lastState.gameRunning) return;
  toggleSettings(false);
  send({cmd: "accountLogout"});
});
$("login-saved-account").addEventListener("click", function () { send({cmd: "accountLoginSaved"}); });
$("backup-refresh").addEventListener("click", function () { send({cmd: "backupRefresh"}); });
$("backup-main").addEventListener("click", function (e) {
  if (e.target.closest(".backup-filter-menu, .backup-filter-button"))
    e.stopPropagation();
  var viewButton = e.target.closest("button[data-backup-view]");
  if (viewButton) {
    backupView = viewButton.dataset.backupView;
    if (backupView !== "upload") setBackupFilterOpen(false);
    if (backupView !== "restore") setRestoreFilterOpen(false);
    document.querySelectorAll("[data-backup-view]").forEach(function (button) {
      button.classList.toggle("on", button.dataset.backupView === backupView);
    });
    document.querySelectorAll(".backup-view").forEach(function (view) {
      view.classList.toggle("on", view.id === "backup-view-" + backupView);
    });
  }
  if (e.target.closest("#backup-filter-button")) {
    setRestoreFilterOpen(false);
    setBackupFilterOpen(!backupFilterOpen);
    return;
  }
  if (e.target.closest("#restore-filter-button")) {
    setBackupFilterOpen(false);
    setRestoreFilterOpen(!restoreFilterOpen);
    return;
  }
  var typeOption = e.target.closest("[data-filter-type]");
  if (typeOption) {
    var type = typeOption.dataset.filterType;
    if (type === "all") backupTypeFilters = {};
    else if (backupTypeFilters[type]) delete backupTypeFilters[type];
    else backupTypeFilters[type] = true;
    renderBackupLocal();
  }
  var folderOption = e.target.closest("[data-filter-folder]");
  if (folderOption) {
    var folder = folderOption.dataset.filterFolder;
    if (folder === "*") backupFolderFilters = Object.create(null);
    else if (backupFolderFilters[folder]) delete backupFolderFilters[folder];
    else backupFolderFilters[folder] = true;
    renderBackupLocal();
  }
  var restoreTypeOption = e.target.closest("[data-restore-filter-type]");
  if (restoreTypeOption) {
    var restoreType = restoreTypeOption.dataset.restoreFilterType;
    if (restoreType === "all") restoreTypeFilters = {};
    else if (restoreTypeFilters[restoreType]) delete restoreTypeFilters[restoreType];
    else restoreTypeFilters[restoreType] = true;
    renderBackupServer();
  }
  var restoreFolderOption = e.target.closest("[data-restore-filter-folder]");
  if (restoreFolderOption) {
    var restoreFolder = restoreFolderOption.dataset.restoreFilterFolder;
    if (restoreFolder === "*") restoreFolderFilters = Object.create(null);
    else if (restoreFolderFilters[restoreFolder]) delete restoreFolderFilters[restoreFolder];
    else restoreFolderFilters[restoreFolder] = true;
    renderBackupServer();
  }
});
document.addEventListener("click", function (e) {
  if (backupFilterOpen && !e.target.closest("#backup-view-upload .backup-toolbar"))
    setBackupFilterOpen(false);
  if (restoreFilterOpen && !e.target.closest("#backup-view-restore .backup-toolbar"))
    setRestoreFilterOpen(false);
});
$("backup-local").addEventListener("change", function (e) {
  var input = e.target.closest("input[data-path]");
  if (!input) return;
  if (input.checked) backupSelectedUploads[input.dataset.path] = true;
  else delete backupSelectedUploads[input.dataset.path];
  updateBackupButtons();
});
$("backup-upload").addEventListener("click", function () {
  var paths = Object.keys(backupSelectedUploads);
  if (paths.length) send({cmd: "backupUpload", paths: paths});
});
$("backup-server").addEventListener("change", function (e) {
  var input = e.target.closest("input[data-id]");
  if (!input || input.disabled) return;
  if (input.checked) backupSelectedRestores[input.dataset.id] = true;
  else delete backupSelectedRestores[input.dataset.id];
  renderBackupServer();
});
$("backup-server").addEventListener("click", function (e) {
  var dateButton = e.target.closest("button[data-backup-date]");
  if (dateButton) {
    var date = dateButton.dataset.backupDate;
    if (backupCollapsedDates[date]) delete backupCollapsedDates[date];
    else backupCollapsedDates[date] = true;
    renderBackupServer();
    return;
  }
  var b = e.target.closest("button[data-id]");
  if (!b || b.disabled) return;
  if (b.dataset.action === "delete") {
    var deleteId = b.dataset.id;
    openConfirmDialog(
      "Delete this backup?",
      "This server backup will be permanently deleted.",
      "Delete",
      true,
      function () { send({cmd: "backupDelete", id: deleteId}); }
    );
  }
});
$("backup-restore-selected").addEventListener("click", function () {
  var ids = Object.keys(backupSelectedRestores);
  if (!ids.length) return;
  var versionsById = {};
  (backupRenderState && backupRenderState.backupVersions || []).forEach(function (version) {
    versionsById[version.id] = version.path;
  });
  var paths = ids.map(function (id) { return versionsById[id] || id; });
  openConfirmDialog(
    "Restore files?",
    "Current files will be preserved before restoring the following versions.",
    "Restore",
    false,
    function () { send({cmd: "backupRestore", ids: ids}); },
    paths
  );
});

/* -- friends ----------------------------------------------------------- */
var MIDDOT = " \u00b7 ";
var friendSig = "";
var artSig = "";
var backupSig = "";
var backupView = "upload";
var backupTypeFilters = {};
var backupFolderFilters = Object.create(null);
var backupFilterOpen = false;
var restoreTypeFilters = {};
var restoreFolderFilters = Object.create(null);
var restoreFilterOpen = false;
var backupCollapsedDates = {};
var backupSelectedUploads = {};
var backupSelectedRestores = {};
var backupRenderState = null;

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
    return '<div class="fr' + (f.online ? " on" : "") + (f.online && f.afk ? " afk" : "") +
      '" data-addr="' + esc(f.address || "") + '"' +
      ' style="animation-delay:' + Math.min(i * 26, 320) + 'ms">' +
      '<span class="st"></span>' +
      '<span class="txt"><span class="nm">' + esc(f.name) + '</span>' +
      '<span class="sv">' + esc(sub) + '</span></span>' +
      (f.online ? '<span class="go">JOIN</span>' : "") +
      '</div>';
  }).join("");
}

function backupPathKey(path) {
  return String(path || "").replace(/\\/g, "/").toLowerCase();
}
function backupExtension(path) {
  var lower = backupPathKey(path);
  var slash = lower.lastIndexOf("/");
  var dot = lower.lastIndexOf(".");
  return dot > slash ? lower.slice(dot) : "";
}
function backupTopFolder(path) {
  var normalized = String(path || "").replace(/\\/g, "/");
  var slash = normalized.indexOf("/");
  return slash < 0 ? "" : normalized.slice(0, slash);
}
function backupFolderExcluded(folder) {
  var lower = String(folder || "").toLowerCase();
  return lower === "dumps" || lower === "downloadedskins" ||
    lower === "communityicons" || lower === "communityicsons";
}
function setBackupFilterOpen(open) {
  backupFilterOpen = !!open;
  $("backup-filter-menu").classList.toggle("on", backupFilterOpen);
  $("backup-filter-button").classList.toggle("on", backupFilterOpen);
  $("backup-filter-button").setAttribute("aria-expanded", backupFilterOpen ? "true" : "false");
}
function setRestoreFilterOpen(open) {
  restoreFilterOpen = !!open;
  $("restore-filter-menu").classList.toggle("on", restoreFilterOpen);
  $("restore-filter-button").classList.toggle("on", restoreFilterOpen);
  $("restore-filter-button").setAttribute("aria-expanded", restoreFilterOpen ? "true" : "false");
}
function renderBackupFilterMenu() {
  if (!backupRenderState) return;
  var folderNames = Object.create(null);
  (backupRenderState.backupFiles || []).forEach(function (file) {
    var folder = backupTopFolder(file.path);
    if (folder && !backupFolderExcluded(folder)) folderNames[folder.toLowerCase()] = folder;
  });
  var folders = Object.keys(folderNames).sort(function (a, b) {
    return folderNames[a].localeCompare(folderNames[b]);
  });
  Object.keys(backupFolderFilters).forEach(function (folder) {
    if (folder !== "" && !folderNames[folder]) delete backupFolderFilters[folder];
  });
  var extensions = [".cfg", ".txt", ".png", ".jpg", ".jpeg", ".log"];
  var hasTypeFilters = Object.keys(backupTypeFilters).length > 0;
  var hasFolderFilters = Object.keys(backupFolderFilters).length > 0;
  var typeHtml = '<div class="backup-filter-title">File type</div>' +
    '<button class="backup-filter-option' + (!hasTypeFilters ? " on" : "") +
    '" type="button" data-filter-type="all">All types</button>' +
    extensions.map(function (extension) {
      return '<button class="backup-filter-option' + (backupTypeFilters[extension] ? " on" : "") +
        '" type="button" data-filter-type="' + extension + '">' + extension + '</button>';
    }).join("");
  var folderHtml = '<div class="backup-filter-title">Folder</div>' +
    '<button class="backup-filter-option' + (!hasFolderFilters ? " on" : "") +
    '" type="button" data-filter-folder="*">All folders</button>' +
    '<button class="backup-filter-option' + (backupFolderFilters[""] ? " on" : "") +
    '" type="button" data-filter-folder="">Root files</button>' +
    folders.map(function (key) {
      return '<button class="backup-filter-option' + (backupFolderFilters[key] ? " on" : "") +
        '" type="button" data-filter-folder="' + esc(key) + '">' + esc(folderNames[key]) + '/</button>';
    }).join("");
  $("backup-filter-menu").innerHTML = typeHtml + folderHtml;
  $("backup-filter-button").classList.toggle("filtered", hasTypeFilters || hasFolderFilters);
}
function renderRestoreFilterMenu() {
  if (!backupRenderState) return;
  var folderNames = Object.create(null);
  (backupRenderState.backupVersions || []).forEach(function (version) {
    var folder = backupTopFolder(version.path);
    if (folder && !backupFolderExcluded(folder)) folderNames[folder.toLowerCase()] = folder;
  });
  var folders = Object.keys(folderNames).sort(function (a, b) {
    return folderNames[a].localeCompare(folderNames[b]);
  });
  Object.keys(restoreFolderFilters).forEach(function (folder) {
    if (folder !== "" && !folderNames[folder]) delete restoreFolderFilters[folder];
  });
  var extensions = [".cfg", ".txt", ".png", ".jpg", ".jpeg", ".log"];
  var hasTypeFilters = Object.keys(restoreTypeFilters).length > 0;
  var hasFolderFilters = Object.keys(restoreFolderFilters).length > 0;
  var typeHtml = '<div class="backup-filter-title">File type</div>' +
    '<button class="backup-filter-option' + (!hasTypeFilters ? " on" : "") +
    '" type="button" data-restore-filter-type="all">All types</button>' +
    extensions.map(function (extension) {
      return '<button class="backup-filter-option' + (restoreTypeFilters[extension] ? " on" : "") +
        '" type="button" data-restore-filter-type="' + extension + '">' + extension + '</button>';
    }).join("");
  var folderHtml = '<div class="backup-filter-title">Folder</div>' +
    '<button class="backup-filter-option' + (!hasFolderFilters ? " on" : "") +
    '" type="button" data-restore-filter-folder="*">All folders</button>' +
    '<button class="backup-filter-option' + (restoreFolderFilters[""] ? " on" : "") +
    '" type="button" data-restore-filter-folder="">Root files</button>' +
    folders.map(function (key) {
      return '<button class="backup-filter-option' + (restoreFolderFilters[key] ? " on" : "") +
        '" type="button" data-restore-filter-folder="' + esc(key) + '">' + esc(folderNames[key]) + '/</button>';
    }).join("");
  $("restore-filter-menu").innerHTML = typeHtml + folderHtml;
  $("restore-filter-button").classList.toggle("filtered", hasTypeFilters || hasFolderFilters);
}
function backupDisabled() {
  return !backupRenderState || backupRenderState.backupBusy || backupRenderState.gameRunning;
}
function updateBackupButtons() {
  var disabled = backupDisabled();
  var uploadCount = Object.keys(backupSelectedUploads).length;
  var restoreCount = Object.keys(backupSelectedRestores).length;
  $("backup-upload").disabled = disabled || !uploadCount;
  $("backup-restore-selected").disabled = disabled || !restoreCount;
  $("backup-refresh").disabled = disabled;
  $("backup-restore-count").textContent = "";
  $("backup-upload").textContent = uploadCount ? "Upload (" + uploadCount + ")" : "Upload";
  $("backup-restore-selected").textContent = restoreCount ? "Restore (" + restoreCount + ")" : "Restore";
}
function renderBackupLocal() {
  if (!backupRenderState) return;
  var disabled = backupDisabled();
  renderBackupFilterMenu();
  var files = (backupRenderState.backupFiles || []).filter(function (file) {
    var hasTypeFilters = Object.keys(backupTypeFilters).length > 0;
    var typeMatches = !hasTypeFilters || !!backupTypeFilters[backupExtension(file.path)];
    var folder = backupTopFolder(file.path).toLowerCase();
    var hasFolderFilters = Object.keys(backupFolderFilters).length > 0;
    var folderMatches = !hasFolderFilters || !!backupFolderFilters[folder];
    return !backupFolderExcluded(folder) && typeMatches && folderMatches;
  });
  $("backup-local").innerHTML = files.length ? files.map(function (file) {
    return '<label class="backup-row"><input type="checkbox" data-path="' + esc(file.path) + '" ' +
      (backupSelectedUploads[file.path] ? "checked " : "") + (disabled ? "disabled" : "") +
      '><span class="grow"><b>' + esc(file.path) + '</b><small>' + fmtBytes(file.size) + '</small></span></label>';
  }).join("") : '<div class="empty"><b>No matching files</b>No allowed files were found for this filter.</div>';
  updateBackupButtons();
}
function renderBackupServer() {
  if (!backupRenderState) return;
  var disabled = backupDisabled();
  renderRestoreFilterMenu();
  var allVersions = (backupRenderState.backupVersions || []).filter(function (version) {
    return !backupFolderExcluded(backupTopFolder(version.path));
  });
  var versions = allVersions.filter(function (version) {
    var folder = backupTopFolder(version.path).toLowerCase();
    var hasTypeFilters = Object.keys(restoreTypeFilters).length > 0;
    var typeMatches = !hasTypeFilters || !!restoreTypeFilters[backupExtension(version.path)];
    var hasFolderFilters = Object.keys(restoreFolderFilters).length > 0;
    var folderMatches = !hasFolderFilters || !!restoreFolderFilters[folder];
    return typeMatches && folderMatches;
  });
  var validIds = {};
  allVersions.forEach(function (version) { validIds[version.id] = true; });
  Object.keys(backupSelectedRestores).forEach(function (id) {
    if (!validIds[id]) delete backupSelectedRestores[id];
  });
  var selectedPaths = {};
  allVersions.forEach(function (version) {
    if (backupSelectedRestores[version.id]) selectedPaths[backupPathKey(version.path)] = version.id;
  });
  var groups = [];
  var byDate = {};
  versions.forEach(function (version) {
    var date = version.createdAt ? String(version.createdAt).slice(0, 10) : "Unknown date";
    if (!byDate[date]) {
      byDate[date] = [];
      groups.push(date);
    }
    byDate[date].push(version);
  });
  $("backup-server").innerHTML = groups.length ? groups.map(function (date) {
    var rows = byDate[date].map(function (version) {
      var selected = !!backupSelectedRestores[version.id];
      var conflict = !!selectedPaths[backupPathKey(version.path)] && !selected;
      var time = String(version.createdAt || "");
      if (time.slice(0, 10) === date && time.length > 11) time = time.slice(11);
      return '<div class="backup-row' + (conflict ? " is-conflict" : "") + '">' +
        '<input type="checkbox" data-id="' + esc(version.id) + '" ' + (selected ? "checked " : "") +
        ((disabled || conflict) ? "disabled" : "") + '><span class="grow"><b>' + esc(version.path) +
        '</b><small>' + esc(time) + '</small></span><button class="danger backup-delete" type="button" data-action="delete" aria-label="Delete backup" data-id="' +
        esc(version.id) + '" ' + (disabled ? "disabled" : "") +
        '><svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M8 9v8m4-8v8m4-8v8M5 6h14m-11 0 1-2h6l1 2m1 0-1 14H8L7 6" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg></button></div>';
    }).join("");
    return '<section class="backup-date-group' + (backupCollapsedDates[date] ? " collapsed" : "") +
      '"><button class="backup-date-toggle" type="button" data-backup-date="' + esc(date) + '">' + esc(date) +
      '</button><div class="backup-list">' + rows + '</div></section>';
  }).join("") : (allVersions.length
    ? '<div class="empty"><b>No matching versions</b>No saved versions match these filters.</div>'
    : '<div class="empty"><b>No saved versions</b>Upload a local file to create one.</div>');
  updateBackupButtons();
}

function renderAccountAndBackup(st) {
  var state = st.accountState || "checking";
  var ready = state === "ready_anonymous" || state === "ready_email";
  $("onboarding").classList.toggle("on", !ready);
  if (state === "checking" || state === "busy") {
    setOnStep("on-wait");
    $("on-wait-title").textContent = state === "busy" ? "Completing your account request..." : "Checking your account...";
  } else if (state === "needs_onboarding") {
    var signIn = (st.accountError || "").indexOf("signin:") === 0;
    if ($("on-wait").classList.contains("on")) setOnStep(signIn ? "on-existing" : "on-choice");
    $("on-error").textContent = signIn ? (st.accountError || "").slice(7) : (st.accountError || "");
  } else if (state === "error") {
    setOnStep("on-wait");
    $("on-wait-title").textContent = "Account check failed";
    $("on-error").innerHTML = esc(st.accountError || "Could not check the account.") +
      '<div class="row-actions"><button class="secondary" type="button" id="account-retry">Retry</button></div>';
    $("account-retry").onclick = function () { send({cmd: "accountRetry"}); };
  } else if (state === "banned") {
    setOnStep("on-wait");
    $("on-wait-title").textContent = "Account suspended";
    $("on-error").textContent = st.accountError || "This account cannot use UClient.";
  }
  $("on-saved-account").style.display = st.hasSavedAccount ? "block" : "none";
  $("saved-account-id").textContent = st.savedAccountInstallId ? ("UUID: " + st.savedAccountInstallId) : "";
  $("backup-link").style.display = state === "ready_anonymous" ? "block" : "none";
  $("backup-main").style.display = state === "ready_email" ? "block" : "none";
  $("backup-error").textContent = st.backupError || st.accountError || "";
  $("account-logout").disabled = !!st.gameRunning;
  $("account-logout").removeAttribute("title");
  $("account-logout").parentElement.classList.toggle("logout-blocked", !!st.gameRunning);
  if (!st.gameRunning) hideLogoutTooltip(true);

  var sig = JSON.stringify([st.backupFiles, st.backupVersions, st.backupBusy, st.gameRunning, st.backupUsed, st.backupLimit]);
  if (sig === backupSig) return;
  backupSig = sig;
  backupRenderState = st;
  backupSelectedUploads = {};
  backupSelectedRestores = {};
  $("backup-usage").textContent =
    "Usage: " + fmtBytes(st.backupUsed || 0) + (st.backupLimit ? " / " + fmtBytes(st.backupLimit) : "");
  renderBackupLocal();
  renderBackupServer();
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

  }
}

/* -- shortcuts / automation -------------------------------------------- */
var activeRailView = "home";
var shortcutsLocal = [];
var shortcutsSig = "";
var scEditing = null;
var scDrawerFilter = "all";
var scPopEl = null;

var SC_TRIGGERS = [{
  id: "chat_received", label: "When chat message received", icon: "\u2709",
  defaults: {type: "chat_received", channel: "all", sender: "everyone", senderName: "", match: "contains", text: "hi"}
}];
var SC_ACTIONS = [
  {id: "send_chat", label: "Send chat message", icon: "\u2709", defaults: {type: "send_chat", channel: "all", message: "hello"}},
  {id: "wait", label: "Wait", icon: "\u23f1", defaults: {type: "wait", seconds: 1}},
  {id: "switch_weapon_use", label: "Switch weapon and use", icon: "\u2692", defaults: {type: "switch_weapon_use", weapon: "hammer"}},
  {id: "set_skin", label: "Set skin", icon: "\u2728", defaults: {type: "set_skin", target: "player", skin: "default"}},
  {id: "set_custom_color", label: "Set custom colors", icon: "\u25cf", defaults: {type: "set_custom_color", target: "player", enabled: true}},
  {id: "set_body_color", label: "Set body color", icon: "\u25cf", defaults: {type: "set_body_color", target: "player", color: 0}},
  {id: "set_feet_color", label: "Set feet color", icon: "\u25cf", defaults: {type: "set_feet_color", target: "player", color: 0}},
  {id: "set_name", label: "Set name", icon: "\u270e", defaults: {type: "set_name", target: "player", name: "name"}}
];

function scUuid() {
  if (window.crypto && crypto.randomUUID) return crypto.randomUUID();
  return "sc-" + Date.now().toString(36) + "-" + Math.random().toString(36).slice(2, 9);
}
function scLabelChannel(ch) {
  if (ch === "team") return "Team";
  if (ch === "uclient") return "UClient";
  return "All";
}
function scLabelWeapon(w) {
  var m = {hammer: "Hammer", gun: "Gun", pistol: "Gun", shotgun: "Shotgun", grenade: "Grenade", laser: "Laser"};
  return m[w] || "Hammer";
}
function scSummaryTrigger(t) {
  if (!t || t.type !== "chat_received") return "When something happens";
  var sender = t.sender === "me" ? "Me" : t.sender === "specific" ? (t.senderName || "Player") : "Everyone";
  return "When " + scLabelChannel(t.channel) + " chat from " + sender + ' "' + (t.text || "") + '"';
}
function scSummaryAction(a) {
  if (!a) return "Action";
  if (a.type === "send_chat") return "Send " + scLabelChannel(a.channel) + ' "' + (a.message || "") + '"';
  if (a.type === "wait") return "Wait " + (a.seconds || 0) + "s";
  if (a.type === "switch_weapon_use") return scLabelWeapon(a.weapon) + " and use";
  if (a.type === "set_skin") return "Set " + a.target + " skin to " + (a.skin || "");
  if (a.type === "set_custom_color") return "Set " + a.target + " custom color " + (a.enabled ? "On" : "Off");
  if (a.type === "set_body_color") return "Set " + a.target + " body color " + (a.color || 0);
  if (a.type === "set_feet_color") return "Set " + a.target + " feet color " + (a.color || 0);
  if (a.type === "set_name") return "Set " + a.target + ' name "' + (a.name || "") + '"';
  return a.type;
}
function scFirstActionLabel(actions) {
  if (!actions || !actions.length) return "Actions";
  return scSummaryAction(actions[0]);
}
function setRailView(view) {
  activeRailView = view;
  $("shell").classList.toggle("shortcuts-mode", view === "shortcuts");
  $("btn-home").classList.toggle("on", view === "home");
  $("btn-shortcuts").classList.toggle("on", view === "shortcuts");
  $("shortcuts-view").setAttribute("aria-hidden", view === "shortcuts" ? "false" : "true");
  if (settingsOpen) toggleSettings(false);
}
$("btn-home").addEventListener("click", function () { setRailView("home"); });
$("btn-shortcuts").addEventListener("click", function () { setRailView("shortcuts"); });

function renderShortcutsList() {
  var list = $("sc-list");
  if (!shortcutsLocal.length) {
    list.innerHTML = '<div class="sc-empty"><b>No automations yet</b>Create your first shortcut to automate chat, weapons, and appearance.</div>';
    return;
  }
  list.innerHTML = shortcutsLocal.map(function (sc) {
    var on = sc.enabled !== false;
    return '<div class="sc-row' + (on ? "" : " off") + '" data-id="' + esc(sc.id) + '">' +
      '<div class="sc-flow"><span class="sc-ico chat">\u2709</span><span class="sc-arrow">\u2192</span><span class="sc-ico action">\u2699</span></div>' +
      '<div class="sc-text"><b>' + esc(sc.name || scSummaryTrigger(sc.trigger)) + '</b><small>Chat</small></div>' +
      '<div class="sc-sw' + (on ? " on" : "") + '" data-toggle="' + esc(sc.id) + '"></div></div>';
  }).join("");
}
function syncShortcutsFromState(st) {
  var sig = JSON.stringify(st.shortcuts || []);
  if (sig === shortcutsSig) return;
  shortcutsSig = sig;
  shortcutsLocal = JSON.parse(sig);
  renderShortcutsList();
}
function scToast(msg) {
  var t = $("sc-toast");
  t.textContent = msg || "Saved";
  t.classList.add("on");
  clearTimeout(scToast._timer);
  scToast._timer = setTimeout(function () { t.classList.remove("on"); }, 1600);
}
function scSaveAll() {
  send({cmd: "shortcutsSave", shortcuts: shortcutsLocal});
  scToast("Saved");
}
function scClosePop() {
  if (scPopEl) { scPopEl.remove(); scPopEl = null; }
}
function scOpenPop(anchor, options, onPick) {
  scClosePop();
  scPopEl = document.createElement("div");
  scPopEl.className = "sc-pop";
  scPopEl.innerHTML = options.map(function (opt) {
    return '<button type="button" data-val="' + esc(String(opt.value)) + '">' + esc(opt.label) + '</button>';
  }).join("");
  document.body.appendChild(scPopEl);
  var r = anchor.getBoundingClientRect();
  scPopEl.style.left = Math.min(r.left, window.innerWidth - scPopEl.offsetWidth - 8) + "px";
  scPopEl.style.top = (r.bottom + 6) + "px";
  scPopEl.addEventListener("click", function (e) {
    var btn = e.target.closest("button[data-val]");
    if (!btn) return;
    onPick(btn.dataset.val);
    scClosePop();
  });
}
document.addEventListener("click", function (e) {
  if (scPopEl && !e.target.closest(".sc-pop") && !e.target.closest(".sc-pill")) scClosePop();
});

function scRenderEditor() {
  var canvas = $("sc-ed-canvas");
  if (!scEditing) { canvas.innerHTML = ""; return; }
  $("sc-ed-title").value = scEditing.name || "New Shortcut";
  var html = "";
  if (scEditing.trigger) {
    html += scBlockHtml("trigger", scEditing.trigger, 0);
  }
  (scEditing.actions || []).forEach(function (act, idx) {
    html += scBlockHtml("action", act, idx);
  });
  canvas.innerHTML = html || '<div class="sc-empty" style="color:#666"><b>No blocks yet</b>Use the drawer below to add a trigger or actions.</div>';
}
function scBlockHtml(kind, data, idx) {
  var del = kind === "action" ? '<button class="sc-del" type="button" data-del-action="' + idx + '">\u00d7</button>' : "";
  if (data.type === "chat_received") {
    return '<div class="sc-block">' + del + '<div class="sc-block-line">When ' +
      scPill("channel", data.channel, idx, kind) + ' chat from ' +
      scPill("sender", data.sender, idx, kind) +
      (data.sender === "specific" ? " " + scPillInput("senderName", data.senderName, idx, kind) : "") +
      " " + scPill("match", data.match, idx, kind) + " " +
      scPillInput("text", data.text, idx, kind) + "</div></div>";
  }
  if (data.type === "send_chat") {
    return '<div class="sc-block">' + del + '<div class="sc-block-line">Send ' +
      scPill("channel", data.channel, idx, kind) + " chat " +
      scPillInput("message", data.message, idx, kind) + "</div></div>";
  }
  if (data.type === "wait") {
    return '<div class="sc-block">' + del + '<div class="sc-block-line">Wait ' +
      scPillInput("seconds", data.seconds, idx, kind) + " seconds</div></div>";
  }
  if (data.type === "switch_weapon_use") {
    return '<div class="sc-block">' + del + '<div class="sc-block-line">Switch to ' +
      scPill("weapon", data.weapon, idx, kind) + " and use</div></div>";
  }
  if (data.type === "set_skin") {
    return '<div class="sc-block">' + del + '<div class="sc-block-line">Set ' +
      scPill("target", data.target, idx, kind) + " skin to " +
      scPillInput("skin", data.skin, idx, kind) + "</div></div>";
  }
  if (data.type === "set_custom_color") {
    return '<div class="sc-block">' + del + '<div class="sc-block-line">Set " +
      scPill("target", data.target, idx, kind) + '" custom colors " +
      scPill("enabled", data.enabled ? "on" : "off", idx, kind) + '"</div></div>';
  }
  if (data.type === "set_body_color" || data.type === "set_feet_color") {
    var part = data.type === "set_body_color" ? "body" : "feet";
    return '<div class="sc-block">' + del + '<div class="sc-block-line">Set ' +
      scPill("target", data.target, idx, kind) + " " + part + " color " +
      scPillInput("color", data.color, idx, kind) + "</div></div>";
  }
  if (data.type === "set_name") {
    return '<div class="sc-block">' + del + '<div class="sc-block-line">Set ' +
      scPill("target", data.target, idx, kind) + " name " +
      scPillInput("name", data.name, idx, kind) + "</div></div>";
  }
  return "";
}
function scPill(field, value, idx, kind) {
  var label = String(value || "");
  if (field === "channel") label = scLabelChannel(value);
  if (field === "sender") label = value === "me" ? "Me" : value === "specific" ? "Specific player" : "Everyone";
  if (field === "match") label = value === "equals" ? "equals" : value === "starts_with" ? "starts with" : "contains";
  if (field === "weapon") label = scLabelWeapon(value);
  if (field === "target") label = value === "dummy" ? "Dummy" : "Player";
  if (field === "enabled") label = value === "on" || value === true ? "On" : "Off";
  return '<button type="button" class="sc-pill" data-field="' + esc(field) + '" data-idx="' + idx + '" data-kind="' + kind + '">' + esc(label) + '</button>';
}
function scPillInput(field, value, idx, kind) {
  return '<button type="button" class="sc-pill input" data-input="' + esc(field) + '" data-idx="' + idx + '" data-kind="' + kind + '">' + esc(String(value == null ? "" : value)) + '</button>';
}
function scGetBlockRef(kind, idx) {
  if (!scEditing) return null;
  return kind === "trigger" ? scEditing.trigger : scEditing.actions[idx];
}
function scSetBlockField(kind, idx, field, value) {
  var ref = scGetBlockRef(kind, idx);
  if (!ref) return;
  if (field === "seconds" || field === "color") ref[field] = Number(value) || 0;
  else if (field === "enabled") ref[field] = value === "on" || value === "true" || value === true;
  else ref[field] = value;
  scRenderEditor();
}
function scOptionsForField(field) {
  if (field === "channel") return [{value: "all", label: "All"}, {value: "team", label: "Team"}, {value: "uclient", label: "UClient"}];
  if (field === "sender") return [{value: "everyone", label: "Everyone"}, {value: "me", label: "Me"}, {value: "specific", label: "Specific player"}];
  if (field === "match") return [{value: "contains", label: "contains"}, {value: "equals", label: "equals"}, {value: "starts_with", label: "starts with"}];
  if (field === "weapon") return [{value: "hammer", label: "Hammer"}, {value: "gun", label: "Gun"}, {value: "shotgun", label: "Shotgun"}, {value: "grenade", label: "Grenade"}, {value: "laser", label: "Laser"}];
  if (field === "target") return [{value: "player", label: "Player"}, {value: "dummy", label: "Dummy"}];
  if (field === "enabled") return [{value: "on", label: "On"}, {value: "off", label: "Off"}];
  return [];
}
function scOpenEditor(existing) {
  scEditing = existing ? JSON.parse(JSON.stringify(existing)) : {
    id: scUuid(), name: "New Shortcut", enabled: true,
    trigger: null, actions: []
  };
  $("sc-ed-delete").style.display = existing ? "inline-block" : "none";
  $("sc-editor").classList.add("on");
  $("sc-editor").setAttribute("aria-hidden", "false");
  $("sc-drawer").classList.remove("expanded");
  scRenderEditor();
  scRenderDrawer();
}
function scCloseEditor() {
  $("sc-editor").classList.remove("on");
  $("sc-editor").setAttribute("aria-hidden", "true");
  scEditing = null;
  scClosePop();
}
function scRenderDrawer() {
  var chips = [{id: "all", label: "All"}, {id: "triggers", label: "Triggers"}, {id: "actions", label: "Actions"}];
  $("sc-drawer-chips").innerHTML = chips.map(function (c) {
    return '<button type="button" class="sc-chip' + (scDrawerFilter === c.id ? " on" : "") + '" data-chip="' + c.id + '">' + c.label + '</button>';
  }).join("");
  var q = ($("sc-drawer-search").value || "").toLowerCase();
  var items = [];
  if (scDrawerFilter === "all" || scDrawerFilter === "triggers") {
    SC_TRIGGERS.forEach(function (t) {
      if (!q || t.label.toLowerCase().indexOf(q) >= 0) items.push({kind: "trigger", def: t});
    });
  }
  if (scDrawerFilter === "all" || scDrawerFilter === "actions") {
    SC_ACTIONS.forEach(function (a) {
      if (!q || a.label.toLowerCase().indexOf(q) >= 0) items.push({kind: "action", def: a});
    });
  }
  $("sc-drawer-list").innerHTML = items.map(function (it) {
    return '<div class="sc-catalog" data-add-kind="' + it.kind + '" data-add-id="' + esc(it.def.id) + '">' +
      '<span class="sc-catalog-ico">' + it.def.icon + '</span><span>' + esc(it.def.label) + '</span></div>';
  }).join("");
}
$("sc-fab").addEventListener("click", function () { scOpenEditor(null); });
$("sc-ed-back").addEventListener("click", function () { scCloseEditor(); });
$("sc-ed-save").addEventListener("click", function () {
  if (!scEditing) return;
  if (!scEditing.trigger) { scToast("Add a trigger"); return; }
  if (!scEditing.actions || !scEditing.actions.length) { scToast("Add at least one action"); return; }
  scEditing.name = $("sc-ed-title").value.trim() || scSummaryTrigger(scEditing.trigger);
  var idx = shortcutsLocal.findIndex(function (s) { return s.id === scEditing.id; });
  if (idx >= 0) shortcutsLocal[idx] = JSON.parse(JSON.stringify(scEditing));
  else shortcutsLocal.push(JSON.parse(JSON.stringify(scEditing)));
  shortcutsSig = JSON.stringify(shortcutsLocal);
  scSaveAll();
  renderShortcutsList();
  scCloseEditor();
});
$("sc-ed-delete").addEventListener("click", function () {
  if (!scEditing) return;
  send({cmd: "shortcutsDelete", id: scEditing.id});
  shortcutsLocal = shortcutsLocal.filter(function (s) { return s.id !== scEditing.id; });
  shortcutsSig = JSON.stringify(shortcutsLocal);
  renderShortcutsList();
  scCloseEditor();
});
$("sc-drawer-handle").addEventListener("click", function () { $("sc-drawer").classList.toggle("expanded"); });
$("sc-drawer-search").addEventListener("input", scRenderDrawer);
$("sc-drawer-chips").addEventListener("click", function (e) {
  var chip = e.target.closest("[data-chip]");
  if (!chip) return;
  scDrawerFilter = chip.dataset.chip;
  scRenderDrawer();
});
$("sc-drawer-list").addEventListener("click", function (e) {
  var row = e.target.closest("[data-add-kind]");
  if (!row || !scEditing) return;
  var kind = row.dataset.addKind;
  var id = row.dataset.addId;
  if (kind === "trigger") {
    var trig = SC_TRIGGERS.find(function (t) { return t.id === id; });
    if (trig) scEditing.trigger = JSON.parse(JSON.stringify(trig.defaults));
  } else {
    var act = SC_ACTIONS.find(function (a) { return a.id === id; });
    if (act) {
      if (!scEditing.actions) scEditing.actions = [];
      scEditing.actions.push(JSON.parse(JSON.stringify(act.defaults)));
    }
  }
  scRenderEditor();
});
$("sc-ed-canvas").addEventListener("click", function (e) {
  if (!scEditing) return;
  var del = e.target.closest("[data-del-action]");
  if (del) {
    scEditing.actions.splice(Number(del.dataset.delAction), 1);
    scRenderEditor();
    return;
  }
  var pill = e.target.closest(".sc-pill");
  if (!pill) return;
  var kind = pill.dataset.kind;
  var idx = Number(pill.dataset.idx || 0);
  if (pill.dataset.input) {
    var field = pill.dataset.input;
    var cur = scGetBlockRef(kind, idx);
    var val = prompt("Enter value", cur ? String(cur[field] == null ? "" : cur[field]) : "");
    if (val != null) scSetBlockField(kind, idx, field, val);
    return;
  }
  var field = pill.dataset.field;
  scOpenPop(pill, scOptionsForField(field), function (val) { scSetBlockField(kind, idx, field, val); });
});
$("sc-list").addEventListener("click", function (e) {
  var toggle = e.target.closest("[data-toggle]");
  if (toggle) {
    e.stopPropagation();
    var id = toggle.dataset.toggle;
    var sc = shortcutsLocal.find(function (s) { return s.id === id; });
    if (!sc) return;
    sc.enabled = !(sc.enabled !== false);
    shortcutsSig = JSON.stringify(shortcutsLocal);
    send({cmd: "shortcutsToggle", id: id, enabled: sc.enabled !== false});
    renderShortcutsList();
    return;
  }
  var row = e.target.closest(".sc-row");
  if (!row) return;
  var sc = shortcutsLocal.find(function (s) { return s.id === row.dataset.id; });
  if (sc) scOpenEditor(sc);
});

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
  renderAccountAndBackup(st);

  var play = $("play");
  var actionBlocked = !!st.playBlocked;
  var accountReady = st.accountState === "ready_anonymous" || st.accountState === "ready_email";
  play.disabled = st.phase !== "ready" || actionBlocked || !accountReady;

  if (st.phase === "ready") {
    setPlayFaceMode(play, "mode-play");
    $("play-label").textContent = st.buttonLabel || "Play";
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
  var launcherVersion = String(st.launcherVersion || "");
  $("settings-launcher-version").textContent = launcherVersion ?
    (launcherVersion.charAt(0).toLowerCase() === "v" ? launcherVersion : "v" + launcherVersion) : "";
  var playWrap = $("play-wrap");
  var showHint = st.phase === "ready" && play.disabled && !!st.buttonHint && !playHintRedundantWithAlert(st);
  playWrap.classList.toggle("hint-on", showHint);
  playWrap.tabIndex = showHint ? 0 : -1;
  $("play-hint-flyout-inner").textContent = showHint ? st.buttonHint : "";
  renderAlerts(st);
  syncShortcutsFromState(st);
  $("ov-status").textContent = playButtonShowsStatus(st.phase) || st.phase === "ready" ? "" : (st.status || "");
  $("ov-status").classList.toggle("bad", !!st.failed && st.phase !== "ready");

  $("up-version").textContent = st.version || "";
  $("up-status").textContent = st.phase === "checking" || st.phase === "launching" ? "" : (st.status || "");
  $("up-status").classList.toggle("bad", !!st.failed);
  $("update-now").hidden = !st.updateAvailable;
  $("update-now").disabled = st.phase !== "ready" || !!st.playBlocked || !!st.gameRunning;
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
