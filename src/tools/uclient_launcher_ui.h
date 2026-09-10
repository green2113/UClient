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
  --spring:cubic-bezier(.32,.72,.24,1);
  --glass-bg:rgba(28,30,38,.72);
  --glass-heavy:rgba(18,20,28,.88);
  --glass-edge:rgba(255,255,255,.12);
  --glass-highlight:rgba(255,255,255,.08);
  --glass-blur:blur(24px) saturate(160%);
  --win-r:12px;
  --play-radius:34px;
  --play-radius-compact:28px;
  --sc-r-sheet:32px;
  --sc-r-block:26px;
  --sc-r-drawer:40px;
  --sc-r-inset:16px;
  --sc-r-icon:50%;
  --sc-r-control:12px;
  --sc-r-pill:14px;
  --sc-pill-fill:rgba(124,108,240,.32);
  --sc-pill-fill-nested:rgba(124,108,240,.52);
  --sc-pill-text:#ddd8ff;
  --sc-pill-text-nested:#f2efff;
  --sc-surface:#181a22;
  --sc-surface-raised:#22242e;
  --sc-surface-inset:#14161c;
  --play-fg:#ffffff;
}
*{margin:0;padding:0;box-sizing:border-box}
#shortcuts-view svg path,#shortcuts-view svg line,#shortcuts-view svg polyline,#shortcuts-view svg rect,#shortcuts-view svg circle,#sc-editor svg path,#sc-editor svg line,#sc-editor svg polyline,#sc-editor svg rect,#sc-editor svg circle{stroke-linecap:round;stroke-linejoin:round}
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
#shortcuts-view{display:none;grid-column:2/4;min-width:0;padding:56px 34px 34px 44px;flex-direction:column;background:linear-gradient(160deg,#12141c 0%,#08090d 100%);position:relative;overflow:hidden}
#shell.shortcuts-mode #shortcuts-view{display:flex}
.sc-head{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px}
.sc-head h1{font:800 32px/1.05 inherit;letter-spacing:-.03em}
.sc-section{color:var(--muted);font-size:13px;font-weight:600;text-transform:uppercase;letter-spacing:.05em;margin:4px 0 10px 4px}
.sc-card{background:var(--glass-bg);backdrop-filter:var(--glass-blur);border:1px solid var(--glass-edge);border-radius:var(--sc-r-block);overflow:hidden;box-shadow:0 4px 24px rgba(0,0,0,.22),inset 0 1px 0 var(--glass-highlight)}
.sc-row{display:grid;grid-template-columns:auto 1fr auto;gap:14px;align-items:center;padding:13px 16px;border-bottom:1px solid rgba(255,255,255,.05);cursor:pointer;transition:background .12s ease-out,opacity .32s var(--spring)}
.sc-row:last-child{border-bottom:0}
.sc-row:hover{background:rgba(255,255,255,.04)}
.sc-row:active{background:rgba(255,255,255,.07);transition:background 0s}
.sc-row.off{opacity:.48}
.sc-flow{display:flex;align-items:center;gap:6px}
.sc-ico{width:32px;height:32px;border-radius:var(--sc-r-icon);display:grid;place-items:center;font-size:14px;background:rgba(255,255,255,.08)}
.sc-ico.chat{background:rgba(52,199,89,.18);color:#34c759}
.sc-ico.action{background:rgba(124,108,240,.22);color:#b8afff}
.sc-arrow{color:var(--muted);font-size:11px;opacity:.7}
.sc-text b{display:block;font-size:15px;font-weight:600;letter-spacing:-.01em;line-height:1.25}
.sc-text small{display:block;color:var(--muted);font-size:13px;line-height:1.35;margin-top:4px}
.sc-sw{width:51px;height:31px;border-radius:99px;background:rgba(255,255,255,.16);position:relative;flex:0 0 auto;cursor:pointer;transition:background .32s var(--spring),box-shadow .32s var(--spring);box-shadow:inset 0 1px 2px rgba(0,0,0,.2)}
.sc-sw::after{content:"";position:absolute;top:2px;left:2px;width:27px;height:27px;border-radius:50%;background:#fff;box-shadow:0 2px 8px rgba(0,0,0,.28);transition:transform .36s var(--spring),width .14s ease-out}
.sc-sw.on{background:#34c759;box-shadow:inset 0 1px 2px rgba(0,0,0,.1),0 0 14px rgba(52,199,89,.24)}
.sc-sw.on::after{transform:translateX(20px)}
.sc-sw:active::after{width:28px}
.sc-sw.on:active::after{transform:translateX(17px)}
.sc-empty{padding:40px 20px;text-align:center;color:var(--muted)}
.sc-empty b{display:block;color:var(--text);font-size:17px;font-weight:600;letter-spacing:-.02em;margin-bottom:8px}
.sc-fab{position:absolute;left:50%;bottom:34px;width:56px;height:56px;border:0;border-radius:50%;background:var(--accent);color:#fff;font-size:28px;line-height:1;cursor:pointer;box-shadow:0 10px 32px rgba(124,108,240,.42),inset 0 1px 0 rgba(255,255,255,.18);z-index:25;transform:translateX(-50%);transition:transform .12s ease-out,background .18s var(--ease);display:none}
.sc-fab:hover{transform:translateX(-50%) scale(1.04);background:var(--accent-hi)}
.sc-fab:active{transform:translateX(-50%) scale(.96);transition:transform 80ms ease-out}
#shortcuts-view.empty .sc-fab{top:58%;bottom:auto;transform:translate(-50%,-50%)}
#shortcuts-view.empty .sc-fab:hover{transform:translate(-50%,-50%) scale(1.04)}
#shortcuts-view.empty .sc-fab:active{transform:translate(-50%,-50%) scale(.96)}
#shell.shortcuts-mode .sc-fab{display:block}
#sc-editor{position:absolute;top:56px;right:16px;bottom:16px;left:16px;z-index:25;display:none;flex-direction:column;border-radius:var(--sc-r-sheet);border:1px solid rgba(255,255,255,.10);background:var(--sc-surface);color:var(--text);box-shadow:0 12px 40px rgba(0,0,0,.38);overflow:clip;transform:translateY(calc(100% + 24px));transition:transform .4s var(--spring);pointer-events:none;will-change:transform}
#sc-editor.on{display:flex;transform:translateY(0);pointer-events:auto}
#shortcuts-view.editing .sc-card,#shortcuts-view.editing .sc-fab{pointer-events:none}
@media (prefers-reduced-motion:reduce){
  #sc-editor{transition:opacity .22s ease;transform:none!important;opacity:0}
  #sc-editor.on{opacity:1}
}
.sc-ed-head{display:flex;align-items:center;gap:10px;padding:12px 14px 12px 8px;background:var(--sc-surface);border-bottom:1px solid rgba(255,255,255,.06);flex:0 0 auto}
.sc-ed-back,.sc-ed-icon{width:40px;height:40px;border:0;border-radius:50%;padding:0;display:grid;place-items:center;cursor:pointer;transition:transform 80ms ease-out,background .15s var(--ease),color .15s var(--ease)}
.sc-ed-back{background:transparent;color:var(--dim)}
.sc-ed-back:hover{background:rgba(255,255,255,.07);color:#fff}
.sc-ed-back:active,.sc-ed-icon:active{transform:scale(.92)}
.sc-ed-back svg,.sc-ed-icon svg{width:20px;height:20px;display:block;flex:0 0 auto;overflow:visible}
.sc-ed-delete svg{width:18px;height:18px}
.sc-ed-back svg{transform:scaleX(-1)}
.sc-ed-title{flex:1;border:0;background:transparent;font:700 17px/1.2 inherit;letter-spacing:-.02em;color:var(--text);outline:none;min-width:0}
.sc-ed-title::placeholder{color:var(--muted)}
.sc-ed-save{background:rgba(124,108,240,.24);color:var(--accent-hi);box-shadow:inset 0 0 0 1px rgba(124,108,240,.28)}
.sc-ed-save:hover{background:rgba(124,108,240,.36)}
.sc-ed-delete{background:rgba(255,59,48,.14);color:#ff6b63;display:none}
.sc-ed-delete:hover{background:rgba(255,59,48,.24)}
.sc-ed-body{flex:1;position:relative;min-height:0;display:flex;flex-direction:column}
.sc-ed-canvas{flex:1;overflow:auto;padding:18px 18px 260px;scroll-behavior:smooth;scrollbar-width:thin;scrollbar-color:rgba(255,255,255,.14) transparent}
.sc-ed-canvas::-webkit-scrollbar{width:8px}
.sc-ed-canvas::-webkit-scrollbar-track{background:transparent}
.sc-ed-canvas::-webkit-scrollbar-thumb{border:2px solid transparent;border-radius:999px;background:rgba(255,255,255,.14);background-clip:padding-box}
.sc-ed-canvas::-webkit-scrollbar-thumb:hover{background:rgba(124,108,240,.45);background-clip:padding-box}
.sc-ed-canvas.drag-over{outline:2px dashed rgba(124,108,240,.45);outline-offset:-8px;border-radius:var(--sc-r-block)}
.sc-ed-empty{margin-top:56px;text-align:center;color:var(--muted);animation:scBlockIn .4s var(--spring) both}
.sc-ed-empty b{display:block;color:var(--dim);font-size:16px;font-weight:600;letter-spacing:-.01em;margin-bottom:8px}
.sc-block{display:flex;gap:10px;align-items:center;background:var(--sc-surface-raised);border:1px solid rgba(255,255,255,.08);border-radius:var(--sc-r-block);padding:14px 14px 14px 10px;margin-bottom:10px;box-shadow:none;position:relative;color:var(--text);transition:transform .18s var(--spring),box-shadow .18s var(--spring),opacity .18s var(--spring),border-color .18s var(--spring)}
.sc-block.sc-enter{animation:scBlockIn .34s var(--spring) both}
.sc-block:hover{border-color:rgba(255,255,255,.14)}
@keyframes scBlockIn{from{opacity:0;transform:translateY(8px) scale(.98)}to{opacity:1;transform:none}}
.sc-block.dragging{opacity:.35;transform:scale(.98)}
.sc-block.drop-target{box-shadow:0 0 0 2px var(--accent),0 8px 24px rgba(124,108,240,.25)}
.sc-if-group{position:relative;margin-bottom:10px;padding-left:20px}
.sc-if-group.sc-enter{animation:scIfGroupIn .36s var(--spring) both}
.sc-if-group::before{content:"";position:absolute;left:8px;top:26px;bottom:26px;width:2px;background:linear-gradient(180deg,rgba(124,108,240,.58),rgba(90,164,240,.38));border-radius:99px;opacity:.82;transition:opacity .22s var(--ease),transform .22s var(--spring);transform-origin:top center}
.sc-if-group:hover::before{opacity:1}
.sc-if-group .sc-block{margin-bottom:8px}
.sc-if-group .sc-block:last-child{margin-bottom:0}
.sc-if-group .sc-block.sc-if-branch{margin-left:16px;position:relative}
.sc-if-group .sc-block.sc-if-branch.sc-enter{animation:scIfBranchIn .32s var(--spring) both}
.sc-if-group .sc-block.sc-if-branch.sc-enter::before{content:"";position:absolute;left:-16px;top:50%;width:12px;height:2px;background:rgba(124,108,240,.38);border-radius:99px;transform:translateY(-50%) scaleX(0);transform-origin:left center;animation:scIfBranchArm .28s var(--spring) .06s both}
.sc-if-group .sc-block.sc-if-branch::before{content:"";position:absolute;left:-16px;top:50%;width:12px;height:2px;background:rgba(124,108,240,.38);border-radius:99px;transform:translateY(-50%) scaleX(1)}
.sc-if-group .sc-block.sc-if-otherwise{border-color:rgba(90,164,240,.24);background:rgba(90,164,240,.06)}
@keyframes scIfGroupIn{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:none}}
@keyframes scIfBranchIn{from{opacity:0;transform:translateX(-6px)}to{opacity:1;transform:none}}
@keyframes scIfBranchArm{from{transform:translateY(-50%) scaleX(0)}to{transform:translateY(-50%) scaleX(1)}}
.sc-block-grip{width:22px;height:28px;border:0;background:transparent;cursor:grab;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;padding:0;margin-top:0;flex:0 0 auto;touch-action:none;align-self:center}
.sc-block-grip span{display:block;width:14px;height:2px;border-radius:99px;background:rgba(255,255,255,.22)}
.sc-block-grip.spacer{cursor:default;opacity:0}
.sc-block-grip:active{cursor:grabbing}
.sc-block-body{flex:1;min-width:0;padding-right:22px;display:flex;align-items:center;min-height:34px}
.sc-block .sc-del{position:absolute;top:50%;right:10px;width:30px;height:30px;border:0;border-radius:50%;background:rgba(255,255,255,.06);color:var(--muted);cursor:pointer;padding:0;display:grid;place-items:center;transform:translateY(-50%);transition:background .12s ease-out,color .12s ease-out,transform 80ms ease-out}
.sc-block .sc-del svg{width:16px;height:16px;display:block;flex:0 0 auto}
.sc-block .sc-del:hover{background:rgba(255,59,48,.18);color:#ff6b63}
.sc-block .sc-del:active{transform:translateY(-50%) scale(.92)}
.sc-block-line,.sc-block-when{display:flex;flex-wrap:wrap;align-items:center;gap:4px 6px;font-size:15px;line-height:1.35;color:var(--text)}
.sc-block-line .sc-txt,.sc-block-when .sc-txt{display:inline-flex;align-items:center;height:30px;line-height:1.35;font-size:15px;font-weight:500;color:var(--dim)}
.sc-pill{display:inline-flex;align-items:center;justify-content:center;gap:0;border:0;border-radius:999px;background:var(--sc-pill-fill);color:var(--sc-pill-text);font:600 14px/1 inherit;height:30px;padding:0 12px;margin:0;cursor:pointer;letter-spacing:-.01em;transition:background .12s ease-out,transform 80ms ease-out;flex:0 0 auto;box-sizing:border-box;box-shadow:none}
.sc-pill.var,.sc-pill.sc-pill-nested{background:var(--sc-pill-fill-nested);color:var(--sc-pill-text-nested);padding:0 12px 0 8px;gap:6px;height:30px}
.sc-pill.var:hover,.sc-pill.sc-pill-nested:hover{background:rgba(124,108,240,.62)}
.sc-pill:hover{background:rgba(124,108,240,.42)}
.sc-pill-icon-svg{width:15px;height:15px;display:block;flex:0 0 auto;opacity:.95}
.sc-pill-label{display:inline-block;line-height:1.2}
.sc-smart-field{position:relative;display:inline-flex;flex-direction:column;align-items:flex-start;vertical-align:top;max-width:100%}
.sc-smart-field.menu-open{z-index:120;isolation:isolate}
.sc-block:has(.sc-smart-field.menu-open),.sc-block-trigger:has(.sc-smart-field.menu-open){position:relative;z-index:130}
.sc-smart-trigger.sc-smart-choice{cursor:pointer}
.sc-smart-menu{position:absolute;left:0;top:calc(100% + 5px);min-width:100%;width:max-content;max-width:min(260px,72vw);z-index:121;opacity:0;transform:translateY(-8px) scale(.96);pointer-events:none;transition:opacity .2s var(--ease),transform .26s var(--spring);transform-origin:top left}
.sc-smart-field.menu-open .sc-smart-menu{opacity:1;transform:translateY(0) scale(1);pointer-events:auto}
.sc-smart-menu.is-open{opacity:1;transform:translateY(0) scale(1);pointer-events:auto}
.sc-smart-menu.sc-smart-menu-portal{position:fixed;left:0;top:0;margin:0;z-index:450;transform:none!important;transform-origin:top left}
.sc-smart-menu-inner{max-height:188px;overflow-y:auto;overflow-x:hidden;border-radius:22px;background:#1e2029;border:1px solid rgba(255,255,255,.12);box-shadow:0 0 0 1px rgba(255,255,255,.05),0 8px 24px rgba(255,255,255,.045),0 16px 40px rgba(0,0,0,.35);padding:6px;scrollbar-width:thin;scrollbar-color:rgba(124,108,240,.45) transparent}
.sc-smart-menu-inner::-webkit-scrollbar{width:7px}
.sc-smart-menu-inner::-webkit-scrollbar-track{background:transparent;margin:4px 0}
.sc-smart-menu-inner::-webkit-scrollbar-thumb{background:linear-gradient(180deg,rgba(124,108,240,.55),rgba(90,164,240,.45));border-radius:99px;border:2px solid transparent;background-clip:padding-box}
.sc-smart-menu-inner::-webkit-scrollbar-thumb:hover{background:linear-gradient(180deg,rgba(140,124,255,.7),rgba(100,180,255,.55));background-clip:padding-box}
.sc-smart-menu-inner::-webkit-scrollbar-button{display:none;width:0;height:0}
.sc-smart-menu-item{display:block;width:100%;text-align:left;border:0;background:transparent;color:var(--text);font:600 13px/1.35 inherit;padding:8px 10px;border-radius:10px;cursor:pointer;transition:background .14s ease-out,color .14s ease-out,transform .12s var(--spring)}
.sc-smart-menu-item.is-var{color:var(--text)}
.sc-smart-menu-head{padding:6px 10px 4px;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.05em;color:var(--dim);pointer-events:none;user-select:none}
.sc-smart-menu-item.sc-smart-menu-text{color:var(--dim);font-weight:600}
.sc-smart-menu-item.sc-smart-menu-clear{color:var(--muted);font-weight:600}
.sc-smart-menu-item.sc-smart-menu-clear:hover{color:#ff8a84;background:rgba(255,69,58,.12)}
.sc-smart-menu-item:hover,.sc-smart-menu-item:focus-visible{background:rgba(124,108,240,.2);outline:none;transform:translateX(1px)}
.sc-smart-menu-sep{height:1px;margin:5px 8px;background:rgba(255,255,255,.09)}
.sc-block.sc-block-text{align-items:flex-start;padding-top:12px;padding-bottom:12px}
.sc-block.sc-block-text .sc-block-ico{align-self:flex-start;margin-top:1px}
.sc-block.sc-block-text .sc-block-body{flex-direction:column;align-items:stretch;gap:8px;min-width:0;padding-right:4px}
.sc-block.sc-block-text .sc-del{top:14px;transform:none}
.sc-text-head{display:flex;align-items:center;min-height:24px}
.sc-text-title{font-size:15px;font-weight:600;color:var(--text);letter-spacing:-.01em;line-height:1.35}
.sc-text-composer{display:flex;flex-wrap:wrap;align-items:center;align-content:flex-start;gap:5px 4px;width:100%;min-height:54px;padding:11px 12px;border-radius:var(--sc-r-inset);background:rgba(0,0,0,.24);border:1px solid rgba(255,255,255,.07);box-shadow:inset 0 1px 0 rgba(255,255,255,.04);line-height:1.45;font-size:15px;transition:border-color .15s var(--ease),background .15s var(--ease),box-shadow .15s var(--ease)}
.sc-text-composer:focus-within{border-color:rgba(124,108,240,.32);background:rgba(0,0,0,.3);box-shadow:inset 0 1px 0 rgba(255,255,255,.05),0 0 0 2px rgba(124,108,240,.14)}
.sc-text-parts{display:contents}
.sc-text-composer .sc-smart-field{vertical-align:middle}
.sc-text-composer .sc-pill.input{background:transparent;box-shadow:none;min-width:28px;width:auto;max-width:100%;height:auto;min-height:26px;line-height:1.45;padding:2px 2px;white-space:pre-wrap;overflow:visible;font-weight:500;color:var(--text)}
.sc-text-composer .sc-pill.input::placeholder{color:var(--muted);opacity:.65;font-weight:500}
.sc-text-composer .sc-pill.input:focus{background:rgba(124,108,240,.1);box-shadow:none;outline:none}
.sc-text-composer .sc-pill.var,.sc-text-composer .sc-pill.sc-smart-choice{height:28px;font-size:14px;padding:0 10px}
.sc-text-composer.sc-text-composer-single{padding:0;border:0;background:transparent;box-shadow:none;min-height:0}
.sc-text-composer.sc-text-composer-single:focus-within{box-shadow:none;border:0;background:transparent}
.sc-text-body-field{display:block;width:100%}
.sc-text-body-input{display:block;width:100%;min-height:54px;max-height:160px;padding:11px 12px;border-radius:var(--sc-r-inset);border:1px solid rgba(255,255,255,.07);background:rgba(0,0,0,.24);box-shadow:inset 0 1px 0 rgba(255,255,255,.04);color:var(--text);font:500 15px/1.45 inherit;resize:vertical;outline:none;transition:border-color .15s var(--ease),background .15s var(--ease),box-shadow .15s var(--ease)}
.sc-text-body-input::placeholder{color:var(--muted);opacity:.65}
.sc-text-body-input:focus{border-color:rgba(124,108,240,.32);background:rgba(0,0,0,.3);box-shadow:inset 0 1px 0 rgba(255,255,255,.05),0 0 0 2px rgba(124,108,240,.14)}
.sc-block-action .sc-block-ico.text{background:rgba(255,214,10,.18);color:#ffd60a}
.sc-pill:active{transform:scale(.97)}
.sc-pill.input{min-width:26px;width:26px;max-width:240px;height:30px;padding:0 12px;text-align:left;cursor:text;box-sizing:border-box;-webkit-appearance:none;appearance:none;line-height:30px;overflow:hidden;white-space:nowrap;font-weight:600;color:var(--sc-pill-text);background:var(--sc-pill-fill);box-shadow:none}
.sc-pill.input.is-truncated{text-overflow:ellipsis}
.sc-pill.input::placeholder{color:rgba(221,216,255,.55);opacity:1;font-weight:600}
.sc-pill.input:focus{background:rgba(124,108,240,.44);box-shadow:none;outline:none;transform:none;text-overflow:clip;overflow-x:auto;color:var(--sc-pill-text-nested)}
.sc-pill-measure{position:absolute;left:-9999px;top:0;visibility:hidden;white-space:pre;pointer-events:none;height:0;overflow:hidden}
.sc-drop-gap{height:52px;border-radius:var(--sc-r-block);border:2px dashed rgba(124,108,240,.55);background:rgba(124,108,240,.08);margin-bottom:12px;animation:scGapPulse .8s var(--spring) infinite alternate}
@keyframes scGapPulse{from{opacity:.55}to{opacity:1}}
.sc-block.sc-run-current{box-shadow:0 0 0 2px rgba(124,108,240,.9),0 10px 28px rgba(124,108,240,.26);background:rgba(124,108,240,.1)}
.sc-block.sc-run-waiting{isolation:isolate;overflow:hidden}
.sc-run-wait-sweep{position:absolute;left:0;top:0;bottom:0;width:0;z-index:0;pointer-events:none;background:linear-gradient(90deg,rgba(124,108,240,.42),rgba(124,108,240,.3));transition:width .12s linear}
.sc-block.sc-run-waiting>:not(.sc-run-wait-sweep):not(.sc-del){position:relative;z-index:1}
.sc-block.sc-run-waiting>.sc-del{z-index:2}
.sc-ed-canvas.sc-run-reflow .sc-block,.sc-ed-canvas.sc-run-reflow .sc-if-group,.sc-ed-canvas.sc-run-reflow .sc-run-result{transition:transform .4s var(--spring)}
.sc-run-result{position:relative;width:100%;margin:-2px 0 12px;padding:10px 18px 0;display:flex;flex-direction:column;align-items:center;box-sizing:border-box}
.sc-if-group .sc-run-result{margin-bottom:8px}
.sc-run-result::before{content:"";position:absolute;left:50%;transform:translateX(-50%);top:-10px;bottom:0;width:2px;border-radius:99px;background:linear-gradient(180deg,rgba(124,108,240,.58),rgba(90,164,240,.42));pointer-events:none;z-index:0}
.sc-run-result.sc-enter{animation:scRunResultIn .42s var(--spring) both}
@keyframes scRunResultIn{from{opacity:0;transform:translateY(-14px) scale(.97)}to{opacity:1;transform:none}}
.sc-run-result-box{position:relative;z-index:1;display:inline-block;max-width:min(100%,420px);padding:9px 13px;border-radius:var(--sc-r-control);background:rgba(255,255,255,.05);box-shadow:inset 0 0 0 1px rgba(255,255,255,.09);font:600 14px/1.35 inherit;color:var(--text);overflow-wrap:anywhere}
.sc-run-result-box em{font-style:normal;color:var(--muted)}
.sc-drawer{--sc-drawer-t:0;position:absolute;left:0;right:auto;bottom:0;width:100%;height:min(78%,520px);background:var(--sc-surface-inset);border-top:1px solid rgba(255,255,255,.08);border-radius:var(--sc-r-drawer) var(--sc-r-drawer) 0 0;box-shadow:none;display:flex;flex-direction:column;overflow:clip;transform:translateY(10%);transition:transform .38s var(--spring),height .38s var(--spring),width .38s var(--spring),left .38s var(--spring),bottom .38s var(--spring),border-radius .38s var(--spring),box-shadow .38s var(--spring);will-change:transform,width,height,left;touch-action:none}
.sc-drawer.dragging{transition:none}
.sc-drawer-chips,.sc-drawer-list{opacity:calc(1 - var(--sc-drawer-t));pointer-events:none;transition:opacity .28s var(--ease)}
.sc-drawer:not(.sc-drawer-capsule) .sc-drawer-chips,.sc-drawer:not(.sc-drawer-capsule) .sc-drawer-list{pointer-events:auto}
.sc-drawer.sc-drawer-capsule .sc-drawer-search{margin-left:12px;margin-right:12px}
.sc-drawer.sc-drawer-capsule .sc-drawer-chips,.sc-drawer.sc-drawer-capsule .sc-drawer-list{display:none}
.sc-drawer-toolbar{display:flex;align-items:center;justify-content:space-between;gap:6px;flex:0 0 auto;max-height:calc(var(--sc-drawer-t) * 54px);padding:calc(var(--sc-drawer-t) * 2px) calc(var(--sc-drawer-t) * 18px) calc(var(--sc-drawer-t) * 12px);overflow:hidden;opacity:var(--sc-drawer-t);pointer-events:none;transition:max-height .34s var(--spring),padding .34s var(--spring),opacity .28s var(--ease)}
.sc-drawer.sc-drawer-capsule .sc-drawer-toolbar{max-height:54px;padding:2px 18px 12px;opacity:1;pointer-events:auto}
.sc-tb-btn{width:40px;height:40px;border:0;border-radius:50%;background:transparent;color:var(--dim);cursor:pointer;padding:0;display:grid;place-items:center;transition:background .14s var(--ease),color .14s var(--ease),transform 80ms ease-out}
.sc-tb-btn svg{width:21px;height:21px;display:block;flex:0 0 auto}
.sc-tb-btn:hover:not(:disabled){background:rgba(255,255,255,.08);color:#fff}
.sc-tb-btn:active:not(:disabled){transform:scale(.9)}
.sc-tb-btn:disabled{opacity:.32;cursor:default}
.sc-tb-btn.sc-tb-play{color:var(--text)}
.sc-drawer-grab{padding:12px 0 8px;cursor:grab;touch-action:none;flex:0 0 auto}
.sc-drawer-grab:active{cursor:grabbing}
.sc-drawer-handle{width:36px;height:5px;border-radius:99px;background:rgba(255,255,255,.32);margin:0 auto;pointer-events:none;transition:background .18s var(--ease),width .18s var(--ease)}
.sc-drawer-grab:hover .sc-drawer-handle{background:rgba(255,255,255,.45);width:42px}
.sc-drawer-search{margin:0 16px 10px;padding:11px 14px;border-radius:22px;border:0;background:rgba(255,255,255,.06);font:15px/1.35 inherit;color:var(--text);outline:none;transition:background .15s var(--ease),box-shadow .15s var(--ease);box-shadow:inset 0 0 0 1px rgba(255,255,255,.08)}
.sc-drawer-search::placeholder{color:var(--muted)}
.sc-drawer-search:focus{background:rgba(255,255,255,.1);box-shadow:inset 0 0 0 1px rgba(124,108,240,.35),0 0 0 3px rgba(124,108,240,.12)}
.sc-drawer-chips{display:flex;gap:8px;padding:0 16px 10px;overflow:auto;flex:0 0 auto}
.sc-chip{border:0;border-radius:999px;background:rgba(255,255,255,.06);color:var(--dim);font:600 13px/1 inherit;padding:8px 14px;cursor:pointer;white-space:nowrap;transition:background .12s ease-out,color .12s ease-out,transform 80ms ease-out}
.sc-chip.on{background:rgba(124,108,240,.24);color:#fff;box-shadow:inset 0 0 0 1px rgba(124,108,240,.22)}
.sc-chip:active{transform:scale(.96)}
.sc-drawer-list{flex:1;overflow:auto;padding:0 12px 20px;-webkit-overflow-scrolling:touch;scrollbar-width:thin;scrollbar-color:rgba(255,255,255,.18) transparent}
.sc-drawer-list::-webkit-scrollbar{width:8px}
.sc-drawer-list::-webkit-scrollbar-track{background:transparent}
.sc-drawer-list::-webkit-scrollbar-thumb{min-height:36px;border:2px solid transparent;border-radius:999px;background:rgba(255,255,255,.16);background-clip:padding-box}
.sc-drawer-list::-webkit-scrollbar-thumb:hover{background:rgba(124,108,240,.55);background-clip:padding-box}
.sc-drawer-list::-webkit-scrollbar-button{display:none;width:0;height:0}
.sc-drawer-chips{scrollbar-width:none}
.sc-drawer-chips::-webkit-scrollbar{display:none}
.sc-catalog{display:flex;align-items:center;gap:12px;padding:11px 12px;border-radius:var(--sc-r-control);cursor:pointer;background:rgba(255,255,255,.04);border:0;margin-bottom:6px;transition:transform 80ms ease-out,background .12s ease-out;box-shadow:inset 0 0 0 1px rgba(255,255,255,.07);user-select:none}
.sc-catalog:hover{background:rgba(255,255,255,.08)}
.sc-catalog:active{transform:scale(.985);background:rgba(255,255,255,.1)}
.sc-catalog-ico{width:34px;height:34px;border-radius:var(--sc-r-icon);display:grid;place-items:center;background:rgba(124,108,240,.18);font-size:15px;flex:0 0 auto}
.sc-catalog-ico.chat{background:rgba(52,199,89,.18);color:#34c759}
.sc-catalog-ico.action{background:rgba(124,108,240,.22);color:#b8afff}
.sc-catalog-ico.wait{background:rgba(255,159,10,.18);color:#ff9f0a}
.sc-catalog-ico.connect{background:rgba(90,164,240,.18);color:#5aa4f0}
.sc-catalog-text{flex:1;min-width:0}
.sc-catalog-text b{display:block;font-size:15px;font-weight:600;letter-spacing:-.01em;color:var(--text);margin-bottom:2px}
.sc-catalog-text small{display:block;font-size:13px;line-height:1.35;color:var(--muted)}
.sc-drawer-section{padding:10px 16px 6px;font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:.05em;color:var(--muted)}
.sc-block-trigger{align-items:flex-start;padding:12px 14px}
.sc-block-trigger-main{flex:1;min-width:0;display:flex;flex-direction:column;align-items:stretch}
.sc-block-trigger-head{display:flex;align-items:center;gap:10px;padding:0;min-height:32px}
.sc-block-trigger-head .sc-block-ico{margin:0;flex:0 0 auto;align-self:center}
.sc-block-trigger-title{flex:1;min-width:0;font-size:15px;line-height:1.35;font-weight:600;letter-spacing:-.01em;color:var(--text);padding:6px 0}
.sc-block-trigger .sc-block-body,.sc-block-trigger-body{display:flex;flex-direction:column;align-items:stretch;align-self:stretch;min-height:0;padding:0;min-width:0;width:100%}
.sc-block-trigger-divider{height:1px;margin:10px 0;width:100%;background:linear-gradient(90deg,transparent,rgba(255,255,255,.1) 8%,rgba(255,255,255,.1) 92%,transparent)}
.sc-block-filters{display:flex;flex-direction:column;gap:0;width:100%;background:rgba(0,0,0,.18);border-radius:var(--sc-r-inset);padding:2px 12px;border:1px solid rgba(255,255,255,.04);box-shadow:none}
.sc-block-filter{display:flex;flex-wrap:wrap;align-items:center;gap:4px 6px;padding:11px 0;min-height:44px;width:100%}
.sc-block-filter+.sc-block-filter{border-top:1px solid rgba(255,255,255,.05)}
.sc-block-filter .sc-txt{color:var(--dim);font-weight:500;font-size:15px;transform:none}
.sc-filter-add{display:inline-flex;align-items:center;gap:5px;border:0;background:transparent;color:var(--accent-hi);font:600 15px/1 inherit;cursor:pointer;padding:12px 2px 4px;text-align:left;letter-spacing:-.01em;transition:color .12s ease-out,transform 80ms ease-out}
.sc-filter-add:hover{color:#fff}
.sc-filter-add:active{transform:scale(.97)}
.sc-filter-del{width:30px;height:30px;border:0;border-radius:50%;background:rgba(255,255,255,.06);color:var(--dim);cursor:pointer;font-size:17px;line-height:1;margin-left:auto;flex:0 0 auto;transition:background .12s ease-out,color .12s ease-out,transform 80ms ease-out}
.sc-filter-del:hover{background:rgba(255,59,48,.18);color:#ff6b63}
.sc-filter-del:active{transform:scale(.92)}
.sc-sender-names{display:inline-flex;flex-wrap:wrap;align-items:center;gap:4px 6px}
.sc-sender-chip{display:inline-flex;align-items:center;height:30px;border-radius:999px;background:var(--sc-pill-fill);color:var(--sc-pill-text);font:600 14px/1 inherit;padding:0 4px 0 11px;box-shadow:none;flex:0 0 auto;max-width:240px}
.sc-sender-chip-label{display:block;max-width:200px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sc-sender-add{width:30px;height:30px;border:0;border-radius:var(--sc-r-pill);background:rgba(124,108,240,.2);color:var(--accent-hi);cursor:pointer;font:700 18px/1 inherit;padding:0;flex:0 0 auto;transition:background .12s ease-out,transform 80ms ease-out}
.sc-sender-add:hover{background:rgba(124,108,240,.34);color:#fff}
.sc-sender-add:active{transform:scale(.92)}
.sc-block-action .sc-block-ico{width:34px;height:34px;border-radius:var(--sc-r-icon);display:grid;place-items:center;font-size:15px;flex:0 0 auto;margin-top:0;align-self:center;line-height:0}
.sc-block-icon-svg{width:18px;height:18px;display:block}
.sc-block-action .sc-block-ico.chat,.sc-block-trigger .sc-block-ico.chat{background:rgba(52,199,89,.18);color:#34c759}
.sc-block-action .sc-block-ico.connect,.sc-block-trigger .sc-block-ico.connect{background:rgba(90,164,240,.18);color:#5aa4f0}
.sc-block-trigger .sc-block-ico{width:34px;height:34px;border-radius:var(--sc-r-icon);display:grid;place-items:center;font-size:15px;flex:0 0 auto}
.sc-block-action .sc-block-ico.action{background:rgba(124,108,240,.22);color:#b8afff}
.sc-block-action .sc-block-ico.wait{background:rgba(255,159,10,.18);color:#ff9f0a}
.sc-block-action .sc-block-ico.flow{background:rgba(100,210,255,.18);color:#64d2ff}
.sc-block-action .sc-block-ico.stop{background:rgba(255,69,58,.18);color:#ff453a}
.sc-block-title{font-size:15px;font-weight:700;color:var(--text);margin-bottom:4px}
.sc-block-when .sc-when-prefix{color:var(--dim);font-weight:600}
.sc-drag-ghost{position:fixed;z-index:500;pointer-events:none;padding:12px 16px;border-radius:var(--sc-r-block);background:rgba(28,30,38,.94);color:var(--text);border:1px solid rgba(255,255,255,.12);box-shadow:0 16px 40px rgba(0,0,0,.48);font:600 14px/1.3 inherit;transform:translate(-50%,-50%);opacity:.95;max-width:min(320px,80vw);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;backdrop-filter:blur(16px)}
.sc-pop{position:fixed;z-index:400;padding:0;background:transparent;border:0;box-shadow:none;overflow:visible;backdrop-filter:none;animation:scPopIn .22s var(--spring) both;transform-origin:top center}
@keyframes scPopIn{from{opacity:0;transform:translateY(-8px) scale(.96)}to{opacity:1;transform:none}}
.sc-pop .sc-smart-menu-inner{min-width:160px;max-width:min(260px,72vw)}
.sc-pop .sc-smart-menu-item.on{background:rgba(124,108,240,.2);color:var(--accent-hi)}
@supports (corner-shape:squircle){
  #play,#sc-editor,.sc-block,.sc-block-filters,.sc-drawer,.sc-drawer-search,.sc-chip,.sc-catalog,.sc-pop,.sc-ed-back,.sc-ed-icon,.sc-ico,.sc-drag-ghost,.sc-drop-gap,.sc-pill,.sc-sender-chip,.sc-sender-add,.sc-smart-menu-inner,.sc-smart-menu-item,.sc-run-result,.sc-run-result-box,.modal-box,.opt{corner-shape:squircle}
}
.sc-toast{position:fixed;left:50%;bottom:90px;transform:translateX(-50%);background:rgba(0,0,0,.82);color:#fff;padding:10px 16px;border-radius:999px;font-size:13px;opacity:0;pointer-events:none;transition:opacity .2s var(--ease);z-index:360;backdrop-filter:blur(12px)}
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
@media (prefers-reduced-transparency:reduce){
  #sc-editor,.sc-card,.sc-block,.sc-drawer,.sc-pop{background:rgba(22,24,32,.97)!important;backdrop-filter:none!important}
}
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
    <div class="sc-head"><h1>Automation (Beta)</h1></div>
    <div class="sc-section">Personal</div>
    <div class="sc-card" id="sc-list"></div>
    <button class="sc-fab" id="sc-fab" type="button" title="Create automation">+</button>
    <div id="sc-editor" aria-hidden="true">
      <div class="sc-ed-head">
        <button class="sc-ed-back" id="sc-ed-back" type="button" title="Back">
          <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M10.5 7.5L16 12l-5.5 4.5" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </button>
        <input class="sc-ed-title" id="sc-ed-title" type="text" placeholder="New Shortcut" maxlength="64">
        <button class="sc-ed-icon sc-ed-delete" id="sc-ed-delete" type="button" title="Delete">
          <svg viewBox="0 0 24 24" fill="none" aria-hidden="true" preserveAspectRatio="xMidYMid meet"><path d="M9 4h6M10 4V3a1 1 0 011-1h2a1 1 0 011 1v1M6 7h12M7 7v12a2 2 0 002 2h6a2 2 0 002-2V7M10 11v5M14 11v5" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </button>
        <button class="sc-ed-icon sc-ed-save" id="sc-ed-save" type="button" title="Save">
          <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M7.5 12.5l3.5 3.5 8-8.5" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </button>
      </div>
      <div class="sc-ed-body">
        <div class="sc-ed-canvas" id="sc-ed-canvas"></div>
        <div class="sc-drawer" id="sc-drawer">
          <div class="sc-drawer-grab" id="sc-drawer-grab"><div class="sc-drawer-handle" id="sc-drawer-handle"></div></div>
          <input class="sc-drawer-search" id="sc-drawer-search" type="search" placeholder="Search actions">
          <div class="sc-drawer-toolbar" id="sc-drawer-toolbar">
            <button class="sc-tb-btn" id="sc-tb-undo" type="button" title="Undo" disabled>
              <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M9 7L5 11l4 4M5 11h8a5 5 0 0 1 5 5v1" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"/></svg>
            </button>
            <button class="sc-tb-btn" id="sc-tb-redo" type="button" title="Redo" disabled>
              <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M15 7l4 4-4 4M19 11h-8a5 5 0 0 0-5 5v1" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"/></svg>
            </button>
            <button class="sc-tb-btn" id="sc-tb-info" type="button" title="Info" disabled>
              <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><circle cx="12" cy="12" r="8.6" stroke="currentColor" stroke-width="1.8"/><path d="M12 11v5.2M12 8.1v.9" stroke="currentColor" stroke-width="1.9" stroke-linecap="round"/></svg>
            </button>
            <button class="sc-tb-btn" id="sc-tb-share" type="button" title="Share" disabled>
              <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M12 15.5V4.2M8.4 7.6L12 4l3.6 3.6M6 13v6.2a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V13" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"/></svg>
            </button>
            <button class="sc-tb-btn sc-tb-play" id="sc-tb-play" type="button" title="Test run">
              <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path fill="currentColor" d="M8.05 5.55a1.35 1.35 0 0 1 2.09-1.12l8.31 5.54a1.35 1.35 0 0 1 0 2.24l-8.31 5.54a1.35 1.35 0 0 1-2.09-1.12V5.55z"/></svg>
            </button>
          </div>
          <div class="sc-drawer-chips" id="sc-drawer-chips"></div>
          <div class="sc-drawer-list" id="sc-drawer-list"></div>
        </div>
      </div>
    </div>
  </section>
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
  if (!scEditing || !(e.ctrlKey || e.metaKey)) return;
  var key = String(e.key || "").toLowerCase();
  if (key !== "z" && key !== "y") return;
  if (e.target.closest("input,textarea")) return;
  e.preventDefault();
  if (key === "y" || e.shiftKey) scRedo();
  else scUndo();
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

/* -- shortcuts / automation -------------------------------------------- */
var activeRailView = "home";
var shortcutsLocal = [];
var shortcutsSig = "";
var scEditing = null;
var scDrawerFilter = "all";
var scPopEl = null;

var SC_TRIGGERS = [{
  id: "chat_received", category: "communication", title: "Chat message",
  whenHint: "When a message matching conditions is received", icon: "\u2709", tone: "chat",
  defaults: {type: "chat_received", channel: "all", filters: [{kind: "message", match: "contains", text: ""}]}
}, {
  id: "server_connect", category: "connection", title: "Server connect",
  whenHint: "When connecting to a specific server", icon: "\u25CE", tone: "connect",
  defaults: {type: "server_connect", targets: []}
}];
var SC_ACTIONS = [
  {id: "get", category: "flow", title: "Get", hint: "Read a value into a variable", icon: "\u2193", tone: "flow", defaults: {type: "get", property: "window_active"}},
  {id: "text", category: "flow", title: "Text", hint: "Combine variables and text into one value", icon: "\u270D", tone: "text", defaults: {type: "text", parts: [{mode: "text", text: ""}], as: "text"}},
  {id: "if", category: "flow", title: "If", hint: "Continue only when a condition matches", icon: "\u2442", tone: "flow", defaults: {type: "if", left: "", op: "contains", right: ""}},
  {id: "otherwise", category: "flow", title: "Otherwise", hint: "Run when the If condition did not match", icon: "\u2443", tone: "flow", defaults: {type: "otherwise"}},
  {id: "end_if", category: "flow", title: "End If", hint: "End an If block", icon: "\u2444", tone: "flow", defaults: {type: "end_if"}},
  {id: "stop", category: "flow", title: "Stop", hint: "Stop running this shortcut", icon: "\u25A0", tone: "stop", defaults: {type: "stop"}},
  {id: "connect_server", category: "connection", title: "Connect to server", hint: "Connect to an IP or IP:port", icon: "\u25CE", tone: "connect", defaults: {type: "connect_server", address: "127.0.0.1:8303"}},
  {id: "leave_server", category: "connection", title: "Leave server", hint: "Disconnect from the current server", icon: "\u21AA", tone: "connect", defaults: {type: "leave_server"}},
  {id: "send_chat", category: "actions", title: "Send message", hint: "Send a variable or text to chat", icon: "\u2709", tone: "chat", defaults: {type: "send_chat", channelMode: "text", channel: "all", uclientRoomMode: "text", uclientRoomId: "", messageMode: "text", messageText: ""}},
  {id: "wait", category: "actions", title: "Wait", hint: "Pause before the next step", icon: "\u23f1", tone: "wait", defaults: {type: "wait", seconds: 1}},
  {id: "switch_weapon_use", category: "actions", title: "Use weapon", hint: "Switch weapon and use it", icon: "\u2692", tone: "action", defaults: {type: "switch_weapon_use", weapon: "hammer"}},
  {id: "set_skin", category: "actions", title: "Set skin", hint: "Change player or dummy skin", icon: "\u2728", tone: "action", defaults: {type: "set_skin", target: "player", skin: "default"}},
  {id: "set_custom_color", category: "actions", title: "Custom colors", hint: "Toggle custom colors on or off", icon: "\u25cf", tone: "action", defaults: {type: "set_custom_color", target: "player", enabled: true}},
  {id: "set_body_color", category: "actions", title: "Body color", hint: "Set body color", icon: "\u25cf", tone: "action", defaults: {type: "set_body_color", target: "player", color: 0}},
  {id: "set_feet_color", category: "actions", title: "Feet color", hint: "Set feet color", icon: "\u25cf", tone: "action", defaults: {type: "set_feet_color", target: "player", color: 0}},
  {id: "set_name", category: "actions", title: "Set name", hint: "Change player or dummy name", icon: "\u270e", tone: "action", defaults: {type: "set_name", target: "player", name: "name"}}
];
var SC_TRIGGER_SECTIONS = [{id: "communication", label: "Communication"}, {id: "connection", label: "Connection"}];
var SC_ACTION_SECTIONS = [{id: "flow", label: "Flow"}, {id: "connection", label: "Connection"}, {id: "actions", label: "Actions"}];

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
function scTriggerDef(type) {
  for (var i = 0; i < SC_TRIGGERS.length; i++) {
    if (SC_TRIGGERS[i].id === type || SC_TRIGGERS[i].defaults.type === type) return SC_TRIGGERS[i];
  }
  return {title: "Trigger", whenHint: "When something happens", icon: "\u26a1", tone: "chat"};
}
function scActionDef(type) {
  for (var i = 0; i < SC_ACTIONS.length; i++) {
    if (SC_ACTIONS[i].id === type || SC_ACTIONS[i].defaults.type === type) return SC_ACTIONS[i];
  }
  return {title: "Action", hint: "Do something", icon: "\u2699", tone: "action"};
}
function scNormalizeSenderFilter(f) {
  var names = [];
  if (f.names && Array.isArray(f.names)) {
    names = f.names.map(function (n) { return n == null ? "" : String(n); });
    while (names.length > 1 && names[names.length - 1] === "" && names[names.length - 2] === "") names.pop();
  } else if (f.sender === "specific" && f.senderName) {
    names = [String(f.senderName)];
  }
  return {kind: "sender", names: names};
}
function scPrepareTriggerForEdit(t) {
  if (!t) return t;
  if (t.type === "server_connect") {
    t = scNormalizeTrigger(JSON.parse(JSON.stringify(t)));
    var targets = Array.isArray(t.targets) ? t.targets.slice() : [];
    targets = targets.map(function (n) { return n == null ? "" : String(n); });
    while (targets.length > 1 && targets[targets.length - 1] === "" && targets[targets.length - 2] === "") targets.pop();
    t.targets = targets;
    return t;
  }
  if (t.type !== "chat_received") return t;
  t = scNormalizeTrigger(JSON.parse(JSON.stringify(t)));
  t.filters = (t.filters || []).map(function (f) {
    if (f.kind !== "sender") return f;
    var names = Array.isArray(f.names) ? f.names.slice() : [];
    names = names.map(function (n) { return n == null ? "" : String(n); });
    while (names.length > 1 && names[names.length - 1] === "" && names[names.length - 2] === "") names.pop();
    return {kind: "sender", names: names};
  });
  return t;
}
function scCleanTriggerForSave(t) {
  t = scNormalizeTrigger(JSON.parse(JSON.stringify(t)));
  if (t.type === "server_connect") {
    t.targets = (t.targets || []).filter(function (n) { return n; });
    return t;
  }
  t.filters = t.filters.map(function (f) {
    if (f.kind === "message") return {kind: "message", match: f.match || "contains", text: f.text || ""};
    if (f.kind === "chat_channel") return {kind: "chat_channel", channel: f.channel || "all"};
    if (f.kind === "uclient_room") return {kind: "uclient_room", room: f.room || ""};
    if (f.kind === "sender") return {kind: "sender", names: (f.names || []).filter(function (n) { return n; })};
    return f;
  }).filter(function (f) {
    // The client treats an empty message filter as "never match", so a blank one
    // would silently disable the whole shortcut.
    if (f.kind === "message") return String(f.text || "").length > 0;
    if (f.kind === "sender") return (f.names || []).length > 0;
    return true;
  });
  return t;
}
function scNormalizeTrigger(t) {
  if (!t) return t;
  if (t.type === "server_connect") {
    var targets = [];
    if (t.targets && Array.isArray(t.targets)) {
      targets = t.targets.map(function (v) { return v == null ? "" : String(v); });
      while (targets.length > 1 && targets[targets.length - 1] === "" && targets[targets.length - 2] === "") targets.pop();
    }
    return {type: "server_connect", targets: targets};
  }
  if (t.type !== "chat_received") return t;
  if (t.filters && t.filters.length) {
    t.filters = t.filters.map(function (f) {
      if (f.kind === "message") return {kind: "message", match: f.match || "contains", text: f.text == null ? "" : String(f.text)};
      if (f.kind === "chat_channel") return {kind: "chat_channel", channel: f.channel || "all"};
      if (f.kind === "uclient_room") return {kind: "uclient_room", room: f.room == null ? "" : String(f.room)};
      return scNormalizeSenderFilter(f);
    });
    return t;
  }
  var filters = [];
  if (t.sender === "specific" && t.senderName) {
    filters.push({kind: "sender", names: [String(t.senderName)]});
  }
  // Legacy sender:"me" has no equivalent in the filter UI, and an empty sender
  // filter would silently widen the trigger to everyone, so it is dropped.
  if (t.text != null && String(t.text).length) {
    filters.push({kind: "message", match: t.match || "contains", text: String(t.text)});
  }
  if (!filters.length) filters.push({kind: "message", match: "contains", text: ""});
  var out = {type: "chat_received", channel: t.channel || "all", filters: filters};
  return out;
}
function scTriggerWhenPreview(t) {
  if (!t) return "When something happens";
  if (t.type === "server_connect") {
    t = scNormalizeTrigger(JSON.parse(JSON.stringify(t)));
    var targets = (t.targets || []).filter(function (n) { return n; });
    if (!targets.length) return "When connecting to a server";
    if (targets.length === 1) return "When connecting to " + targets[0];
    return "When connecting to " + targets.join(" or ");
  }
  if (t.type !== "chat_received") return "When something happens";
  t = scNormalizeTrigger(JSON.parse(JSON.stringify(t)));
  var msg = "a chat message";
  if (t.channel === "team") msg = "a team chat message";
  else if (t.channel === "uclient") msg = "a UClient chat message";
  var parts = [];
  t.filters.forEach(function (f) {
    if (f.kind === "sender") {
      var names = (f.names || []).filter(function (n) { return n; });
      if (names.length === 1) parts.push("from " + names[0]);
      else if (names.length > 1) parts.push("from " + names.join(" or "));
    } else if (f.kind === "chat_channel") {
      parts.push("on " + scLabelChannel(f.channel || "all") + " chat");
    } else if (f.kind === "uclient_room") {
      if (f.room) parts.push('in UClient room "' + f.room + '"');
      else parts.push("in any UClient room");
    } else if (f.text) {
      var match = f.match || "contains";
      if (match === "equals") parts.push('text equals "' + f.text + '"');
      else if (match === "starts_with") parts.push('text starts with "' + f.text + '"');
      else parts.push('text contains "' + f.text + '"');
    }
  });
  if (!parts.length) return "When " + msg + " is received";
  return "When " + msg + " " + parts.join(" and ");
}
function scCatalogMatches(def, q, isTrigger) {
  if (!q) return true;
  q = q.toLowerCase();
  var hay = [
    def.title || "", def.hint || "", def.whenHint || "", def.label || "",
    isTrigger ? "when" : "do"
  ].join(" ").toLowerCase();
  return hay.indexOf(q) >= 0;
}
function scIconX() {
  return '<svg viewBox="0 0 24 24" fill="none" aria-hidden="true" preserveAspectRatio="xMidYMid meet"><path d="M7 7l10 10M17 7L7 17" stroke="currentColor" stroke-width="2.25" stroke-linecap="round"/></svg>';
}
function scCatalogRow(kind, def) {
  var isTrigger = kind === "trigger";
  var title = def.title || def.label || def.id;
  var sub = isTrigger ? (def.whenHint || scTriggerWhenPreview(def.defaults)) : (def.hint || def.label || title);
  var tone = def.tone || (isTrigger ? "chat" : "action");
  return '<div class="sc-catalog" data-add-kind="' + kind + '" data-add-id="' + esc(def.id) + '">' +
    '<span class="sc-catalog-ico ' + tone + '">' + def.icon + '</span>' +
    '<div class="sc-catalog-text"><b>' + esc(title) + '</b><small>' + esc(sub) + '</small></div></div>';
}
function scSummaryTrigger(t) {
  return scTriggerWhenPreview(t);
}
function scLabelGetProperty(prop) {
  if (prop === "window_active") return "Window Active";
  return prop || "Value";
}
function scLabelIfOp(op) {
  if (op === "is_not") return "is not";
  if (op === "has_any") return "has any value";
  if (op === "has_none") return "does not have any value";
  if (op === "contains") return "contains";
  if (op === "not_contains") return "does not contain";
  if (op === "starts_with") return "begins with";
  if (op === "ends_with") return "ends with";
  return "is";
}
function scIfLeftValueKind(varId) {
  if (varId === "messageChannel") return "channel";
  return "text";
}
function scIfOpsForVariable(varId) {
  if (!varId) return [];
  if (varId === "messageChannel") {
    return [{value: "is", label: "is"}, {value: "is_not", label: "is not"}];
  }
  return [
    {value: "has_any", label: "has any value"},
    {value: "has_none", label: "does not have any value"},
    {value: "contains", label: "contains"},
    {value: "not_contains", label: "does not contain"},
    {value: "starts_with", label: "begins with"},
    {value: "ends_with", label: "ends with"},
    {value: "is", label: "is"},
    {value: "is_not", label: "is not"}
  ];
}
function scIfOpNeedsRight(op) {
  return op !== "has_any" && op !== "has_none";
}
function scNormalizeIfCondition(ifAction) {
  if (!ifAction || ifAction.type !== "if") return;
  if (!ifAction.left) {
    ifAction.op = "contains";
    ifAction.right = "";
    return;
  }
  var ops = scIfOpsForVariable(ifAction.left);
  if (!ifAction.op || !ops.some(function (o) { return o.value === ifAction.op; })) {
    ifAction.op = ops.length ? ops[0].value : "contains";
  }
  if (!scIfOpNeedsRight(ifAction.op)) {
    ifAction.right = "";
    return;
  }
  if (ifAction.right == null) ifAction.right = "";
  if (scIfLeftValueKind(ifAction.left) === "channel" && !ifAction.right) ifAction.right = "all";
}
function scLabelVariable(id) {
  var vars = scAvailableVariables(null);
  for (var i = 0; i < vars.length; i++) {
    if (vars[i].id === id) return vars[i].label;
  }
  if (id === "messageSender" || id === "senderName") return "Message Sender";
  if (id === "messageText" || id === "message") return "Message Text";
  if (id === "messageChannel") return "Message Channel";
  if (id === "messageUClientRoom") return "Message UClient Room";
  if (id === "messageUClientRoomId") return "Message UClient Room ID";
  if (id === "window_active") return "Window Active";
  if (id === "name") return "My Name";
  if (id === "nearestPlayer") return "Nearest Player";
  return id || "Variable";
}
function scAvailableVariables(beforeIdx) {
  var vars = [];
  var limit = beforeIdx == null
    ? (scEditing && scEditing.actions ? scEditing.actions.length : 0)
    : beforeIdx;
  if (scEditing && scEditing.trigger && scEditing.trigger.type === "chat_received") {
    vars.push({id: "messageSender", label: "Message Sender"});
    vars.push({id: "messageText", label: "Message Text"});
    vars.push({id: "messageChannel", label: "Message Channel"});
    vars.push({id: "messageUClientRoom", label: "Message UClient Room"});
    vars.push({id: "messageUClientRoomId", label: "Message UClient Room ID"});
  }
  if (scEditing && scEditing.actions) {
    // Read the raw actions on purpose: normalizing an if block would ask for the
    // variable list again and make this grow exponentially.
    for (var i = 0; i < limit; i++) {
      var a = scEditing.actions[i];
      if (!a) continue;
      if (a.type === "get" && a.property === "window_active")
        vars.push({id: "window_active", label: "Window Active"});
      if (a.type === "text") {
        var as = a.as || ("text_" + i);
        vars.push({id: as, label: as === "text" ? "Text" : as});
      }
    }
  }
  return vars;
}
function scSmartFieldVariables(slot, beforeIdx) {
  var all = scAvailableVariables(beforeIdx);
  if (slot === "channel") {
    return all.filter(function (v) { return v.id === "messageChannel"; });
  }
  if (slot === "uclientRoom") {
    return all.filter(function (v) { return v.id === "messageUClientRoomId"; });
  }
  if (slot === "message" || slot === "textPart") {
    return all.filter(function (v) {
      return v.id !== "messageChannel" && v.id !== "messageUClientRoom" &&
        v.id !== "messageUClientRoomId";
    });
  }
  return all;
}
function scNextTextVarName() {
  if (!scEditing || !scEditing.actions) return "text";
  var used = Object.create(null);
  scEditing.actions.forEach(function (a) {
    if (a && a.type === "text" && a.as) used[a.as] = true;
  });
  if (!used["text"]) return "text";
  for (var n = 1; n < 1000; n++) {
    if (!used["text_" + n]) return "text_" + n;
  }
  return "text_" + Date.now();
}
function scNormalizeAction(a, actionIdx) {
  if (!a) return a;
  if (a.type === "send_chat") {
    if (a.channel != null && typeof a.channel === "string") {
      a.channelMode = "text";
    } else if (a.channel && typeof a.channel === "object") {
      a.channelMode = a.channel.mode || "text";
      if (a.channelMode === "variable") a.channelVariable = a.channel.variable || "messageChannel";
      else a.channel = a.channel.text || "all";
    }
    if (!a.channelMode) { a.channelMode = "text"; a.channel = a.channel || "all"; }
    if (a.uclientRoom != null && typeof a.uclientRoom === "string") {
      a.uclientRoomMode = "text";
      a.uclientRoomId = a.uclientRoom;
      delete a.uclientRoom;
    } else if (a.uclientRoom && typeof a.uclientRoom === "object") {
      a.uclientRoomMode = a.uclientRoom.mode || "text";
      if (a.uclientRoomMode === "variable") a.uclientRoomVariable = a.uclientRoom.variable || "messageUClientRoomId";
      else a.uclientRoomId = a.uclientRoom.text != null ? String(a.uclientRoom.text) : "";
      delete a.uclientRoom;
    }
    if (!a.uclientRoomMode) { a.uclientRoomMode = "text"; a.uclientRoomId = a.uclientRoomId || ""; }
    if (a.message != null && typeof a.message === "string") {
      a.messageMode = "text";
      a.messageText = a.message;
      delete a.message;
    } else if (a.message && typeof a.message === "object") {
      a.messageMode = a.message.mode || "text";
      if (a.messageMode === "variable") a.messageVariable = a.message.variable || "messageText";
      else a.messageText = a.message.text != null ? String(a.message.text) : "";
      delete a.message;
    }
    if (!a.messageMode) {
      a.messageMode = "text";
      a.messageText = a.messageText == null ? "" : String(a.messageText);
    }
  }
  if (a.type === "wait") {
    a.seconds = scClampWaitSeconds(a.seconds);
  }
  if (a.type === "if") {
    if (a.left == null) a.left = "";
    if (a.left) {
      var ifVars = scAvailableVariables(actionIdx != null ? actionIdx : 0);
      if (!ifVars.some(function (v) { return v.id === a.left; })) a.left = "";
    }
    scNormalizeIfCondition(a);
  }
  if (a.type === "text") {
    if (!a.parts || !a.parts.length) a.parts = [{mode: "text", text: ""}];
    a.parts = scMergeTextParts(a.parts.map(function (p) {
      if (p.mode === "variable") return {mode: "variable", variable: p.variable || "senderName"};
      return {mode: "text", text: p.text == null ? "" : String(p.text)};
    }));
    if (!a.as) a.as = actionIdx != null ? ("text_" + actionIdx) : "text";
  }
  return a;
}
function scCleanActionForSave(a) {
  a = JSON.parse(JSON.stringify(a));
  if (a.type === "send_chat") {
    if (a.channelMode === "variable")
      a.channel = {mode: "variable", variable: a.channelVariable || "messageChannel"};
    else
      a.channel = {mode: "text", text: a.channel || "all"};
    if (a.uclientRoomMode === "variable")
      a.uclientRoom = {mode: "variable", variable: a.uclientRoomVariable || "messageUClientRoomId"};
    else
      a.uclientRoom = {mode: "text", text: a.uclientRoomId || ""};
    if (a.messageMode === "variable")
      a.message = {mode: "variable", variable: a.messageVariable || "messageText"};
    else
      a.message = {mode: "text", text: a.messageText || ""};
    delete a.channelMode; delete a.channelVariable;
    delete a.uclientRoomMode; delete a.uclientRoomId; delete a.uclientRoomVariable;
    delete a.messageMode; delete a.messageText; delete a.messageVariable;
  }
  if (a.type === "text") {
    a.parts = scMergeTextParts((a.parts || []).filter(function (p) {
      if (p.mode === "variable") return !!p.variable;
      return p.text != null && String(p.text).length > 0;
    }));
    if (!a.parts.length) a.parts = [{mode: "text", text: ""}];
    if (!a.as) a.as = "text";
  }
  return a;
}
var scSmartBlurTimer = null;
var SC_CHANNEL_CHOICES = [{value: "all", label: "All"}, {value: "team", label: "Team"}, {value: "uclient", label: "UClient"}];
function scBlockToneIconSvg(tone) {
  var sw = "1.25";
  var rnd = ' stroke-linecap="round" stroke-linejoin="round"';
  if (tone === "chat") {
    return '<svg class="sc-block-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M2.8 3.4h10.4a1.5 1.5 0 0 1 1.5 1.5v5.8a1.5 1.5 0 0 1-1.5 1.5H8.1L5.2 13.9v-2.7H2.8a1.5 1.5 0 0 1-1.5-1.5V4.9a1.5 1.5 0 0 1 1.5-1.5z"/></svg>';
  }
  if (tone === "text") {
    return '<svg class="sc-block-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M3.8 4.4h8.4M3.8 8h6.6M3.8 11.6h4.8"/></svg>';
  }
  if (tone === "flow") {
    return '<svg class="sc-block-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M3.2 4.2h9.6M3.2 8h5.8M3.2 11.8h7.6"/><circle fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' cx="12.2" cy="8" r="1.85"/></svg>';
  }
  if (tone === "connect") {
    return '<svg class="sc-block-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><circle fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' cx="8" cy="8" r="2.5"/><circle fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' cx="8" cy="8" r="5.2" stroke-dasharray="2.1 2.1"/></svg>';
  }
  if (tone === "wait") {
    return '<svg class="sc-block-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><circle fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' cx="8" cy="8.5" r="5"/><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M8 5.6V8.4l1.9 1.1"/></svg>';
  }
  if (tone === "stop") {
    return '<svg class="sc-block-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><rect fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' x="4.2" y="4.2" width="7.6" height="7.6" rx="3"/></svg>';
  }
  return '<svg class="sc-block-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M4.2 8h7.6M8 4.2v7.6"/></svg>';
}
function scBlockIconInner(tone, fallback) {
  return scBlockToneIconSvg(tone) || fallback;
}
function scVarPillIconSvg(varId) {
  var id = varId || "";
  var sw = "1.25";
  var rnd = ' stroke-linecap="round" stroke-linejoin="round"';
  if (id.indexOf("message") === 0 || id === "senderName" || id === "sender" || id === "message") {
    return '<svg class="sc-pill-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M2.8 3.4h10.4a1.5 1.5 0 0 1 1.5 1.5v5.8a1.5 1.5 0 0 1-1.5 1.5H8.1L5.2 13.9v-2.7H2.8a1.5 1.5 0 0 1-1.5-1.5V4.9a1.5 1.5 0 0 1 1.5-1.5z"/></svg>';
  }
  if (id === "window_active") {
    return '<svg class="sc-pill-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><rect fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' x="2.6" y="3.2" width="10.8" height="9.6" rx="2.6"/><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M5.2 3.2V2.5M10.8 3.2V2.5"/></svg>';
  }
  if (id === "text" || id.indexOf("text") === 0) {
    return '<svg class="sc-pill-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M3.8 4.4h8.4M3.8 8h6.6M3.8 11.6h4.8"/></svg>';
  }
  return '<svg class="sc-pill-icon-svg" viewBox="0 0 16 16" aria-hidden="true"><circle fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' cx="8" cy="8" r="2.1"/><path fill="none" stroke="currentColor" stroke-width="' + sw + '"' + rnd + ' d="M8 2.6v1.2M8 12.2v1.2M2.6 8h1.2M12.2 8h1.2"/></svg>';
}
function scVarPillButton(extraClass, varId, label, attrs) {
  return '<button type="button" class="sc-pill var sc-pill-nested' + (extraClass ? " " + extraClass : "") + '"' + (attrs || "") + ">" +
    scVarPillIconSvg(varId) + '<span class="sc-pill-label">' + esc(label) + "</span></button>";
}
function scSmartMenuHtml(beforeIdx, fixedChoices, menuMode, slot) {
  var vars = scSmartFieldVariables(slot, beforeIdx);
  var html = '<div class="sc-smart-menu" aria-hidden="true"><div class="sc-smart-menu-inner">';
  var showVars = vars.length > 0;
  if (menuMode === "variable") {
    html += '<button type="button" class="sc-smart-menu-item sc-smart-menu-clear" data-pick-type="clear" data-pick-id="">Clear Variable</button>';
    if (showVars) html += '<div class="sc-smart-menu-sep"></div>';
  }
  if (menuMode !== "variable" && fixedChoices && fixedChoices.length) {
    fixedChoices.forEach(function (c) {
      html += '<button type="button" class="sc-smart-menu-item" data-pick-type="fixed" data-pick-id="' + esc(c.value) + '">' + esc(c.label) + '</button>';
    });
    if (showVars) html += '<div class="sc-smart-menu-sep"></div>';
  }
  if (showVars) html += '<div class="sc-smart-menu-head">Variable</div>';
  vars.forEach(function (v) {
    html += '<button type="button" class="sc-smart-menu-item is-var" data-pick-type="var" data-pick-id="' + esc(v.id) + '">' + esc(v.label) + '</button>';
  });
  html += '</div></div>';
  return html;
}
function scSmartFieldHtml(opts) {
  var isVar = opts.mode === "variable";
  var html = '<span class="sc-smart-field" data-smart-slot="' + esc(opts.slot) + '" data-idx="' + opts.idx + '" data-kind="' + esc(opts.kind) + '"';
  if (opts.partIdx != null) html += ' data-part-idx="' + opts.partIdx + '"';
  html += '>';
  if (isVar) {
    html += scVarPillButton("sc-smart-trigger", opts.varValue || "", scLabelVariable(opts.varValue || ""), ' data-smart-trigger="var"');
  } else if (opts.choice) {
    html += '<button type="button" class="sc-pill sc-smart-trigger sc-smart-choice" data-smart-trigger="choice">' +
      esc(opts.choiceLabel || scLabelChannel(opts.textValue) || "All") + '</button>';
  } else {
    html += scPillInput(opts.textInputField, opts.textValue, opts.idx, opts.kind, opts.partIdx, "sc-smart-trigger");
  }
  html += scSmartMenuHtml(opts.beforeIdx != null ? opts.beforeIdx : opts.idx, opts.fixedChoices, isVar ? "variable" : "input", opts.slot);
  html += '</span>';
  return html;
}
function scSmartMenuForField(fieldEl) {
  if (!fieldEl) return null;
  return fieldEl._scMenuPortaled || fieldEl.querySelector(".sc-smart-menu");
}
function scSmartMenuPortalAttach(fieldEl, menu) {
  if (!fieldEl || !menu || menu.dataset.scPortaled === "1") return;
  menu.dataset.scPortaled = "1";
  menu._scPortalField = fieldEl;
  fieldEl._scMenuPortaled = menu;
  document.body.appendChild(menu);
}
function scSmartMenuPortalDetach(fieldEl, menu) {
  if (!fieldEl || !menu) return;
  delete menu.dataset.scPortaled;
  delete menu._scPortalField;
  delete fieldEl._scMenuPortaled;
  fieldEl.appendChild(menu);
}
function scCloseAllSmartMenus(except) {
  (document.querySelectorAll(".sc-smart-field.menu-open") || []).forEach(function (el) {
    if (el !== except) scSmartFieldHideMenu(el);
  });
}
function scCloseAllPickers(exceptSmartField) {
  scClosePop();
  scCloseAllSmartMenus(exceptSmartField || null);
}
function scResetSmartMenuPosition(menu) {
  if (!menu) return;
  menu.classList.remove("sc-smart-menu-portal", "is-open");
  menu.style.left = "";
  menu.style.top = "";
  menu.style.width = "";
  menu.style.minWidth = "";
  menu.style.maxWidth = "";
}
function scPositionSmartMenu(fieldEl) {
  var menu = scSmartMenuForField(fieldEl);
  if (!menu || !fieldEl.classList.contains("menu-open")) return;
  var trigger = fieldEl.querySelector(".sc-smart-trigger") || fieldEl;
  var r = trigger.getBoundingClientRect();
  if (r.width < 1 && r.height < 1) return;
  menu.classList.add("sc-smart-menu-portal", "is-open");
  menu.style.top = (r.bottom + 5) + "px";
  menu.style.minWidth = Math.max(r.width, 160) + "px";
  menu.style.maxWidth = "min(260px, 72vw)";
  menu.style.width = "max-content";
  menu.style.left = r.left + "px";
  requestAnimationFrame(function () {
    if (!fieldEl.classList.contains("menu-open")) return;
    var mw = menu.offsetWidth;
    var mh = menu.offsetHeight;
    var left = Math.max(8, Math.min(r.left, window.innerWidth - mw - 8));
    var top = r.bottom + 5;
    if (top + mh > window.innerHeight - 8) top = Math.max(8, r.top - mh - 5);
    menu.style.left = left + "px";
    menu.style.top = top + "px";
  });
}
var scSmartMenuLayoutBound = false;
function scEnsureSmartMenuLayoutListeners() {
  if (scSmartMenuLayoutBound) return;
  scSmartMenuLayoutBound = true;
  var canvas = $("sc-ed-canvas");
  if (canvas) {
    canvas.addEventListener("scroll", function () {
      (document.querySelectorAll(".sc-smart-field.menu-open") || []).forEach(scPositionSmartMenu);
    }, {passive: true});
  }
  window.addEventListener("resize", function () {
    (document.querySelectorAll(".sc-smart-field.menu-open") || []).forEach(scPositionSmartMenu);
  }, {passive: true});
}
function scSmartFieldShowMenu(fieldEl, force) {
  if (!fieldEl) return;
  var menu = fieldEl.querySelector(".sc-smart-menu");
  if (!menu) return;
  var input = fieldEl.querySelector("input.sc-smart-trigger, textarea.sc-smart-trigger");
  if (input && !force && String(input.value || "").length > 0) {
    scSmartFieldHideMenu(fieldEl);
    return;
  }
  scClosePop();
  scCloseAllSmartMenus(fieldEl);
  scEnsureSmartMenuLayoutListeners();
  scSmartMenuPortalAttach(fieldEl, menu);
  fieldEl.classList.add("menu-open");
  menu.setAttribute("aria-hidden", "false");
  requestAnimationFrame(function () {
    scPositionSmartMenu(fieldEl);
    requestAnimationFrame(function () { scPositionSmartMenu(fieldEl); });
  });
}
function scSmartFieldHideMenu(fieldEl) {
  if (!fieldEl) return;
  fieldEl.classList.remove("menu-open");
  var menu = scSmartMenuForField(fieldEl);
  if (menu) {
    menu.setAttribute("aria-hidden", "true");
    scResetSmartMenuPosition(menu);
    scSmartMenuPortalDetach(fieldEl, menu);
  }
}
function scApplySmartPick(fieldEl, pickType, pickId) {
  if (!fieldEl) return;
  var slot = fieldEl.dataset.smartSlot;
  var idx = Number(fieldEl.dataset.idx || 0);
  var kind = fieldEl.dataset.kind;
  var partIdx = fieldEl.dataset.partIdx != null ? Number(fieldEl.dataset.partIdx) : null;
  var ref = scGetBlockRef(kind, idx);
  if (!ref) return;
  scPushHistory();
  scNormalizeAction(ref, idx);
  if (pickType === "clear" || pickType === "text") {
    if (slot === "message") { ref.messageMode = "text"; ref.messageText = ""; }
    else if (slot === "channel") { ref.channelMode = "text"; ref.channel = ref.channel || "all"; }
    else if (slot === "uclientRoom") { ref.uclientRoomMode = "text"; ref.uclientRoomId = ""; }
    else if (slot === "textPart" && partIdx != null) ref.parts[partIdx] = {mode: "text", text: ""};
    else if (slot === "textBody") ref.parts = [{mode: "text", text: ""}];
    else if (slot === "ifLeft") { ref.left = ""; ref.op = "contains"; ref.right = ""; }
  } else if (pickType === "fixed") {
    if (slot === "channel") {
      ref.channelMode = "text";
      ref.channel = pickId || "all";
    }
  } else if (pickType === "var") {
    if (slot === "message") { ref.messageMode = "variable"; ref.messageVariable = pickId; }
    else if (slot === "channel") { ref.channelMode = "variable"; ref.channelVariable = pickId; }
    else if (slot === "uclientRoom") { ref.uclientRoomMode = "variable"; ref.uclientRoomVariable = pickId; }
    else if (slot === "textPart" && partIdx != null) {
      ref.parts[partIdx] = {mode: "variable", variable: pickId};
      if (partIdx === ref.parts.length - 1) ref.parts.push({mode: "text", text: ""});
    } else if (slot === "textBody") {
      var bodyText = "";
      if (ref.parts.length === 1 && ref.parts[0].mode === "text") bodyText = ref.parts[0].text || "";
      ref.parts = [{mode: "text", text: bodyText}, {mode: "variable", variable: pickId}, {mode: "text", text: ""}];
    } else if (slot === "ifLeft") {
      ref.left = pickId || "";
      scNormalizeIfCondition(ref);
    }
  }
  scSmartFieldHideMenu(fieldEl);
  scUpdateBlockElement(kind, idx);
}
function scIfLeftFieldHtml(data, idx, kind) {
  var left = data.left || "";
  var html = '<span class="sc-smart-field" data-smart-slot="ifLeft" data-idx="' + idx + '" data-kind="' + esc(kind) + '">';
  if (left) {
    html += scVarPillButton("sc-smart-trigger", left, scLabelVariable(left) || left, ' data-smart-trigger="var"');
    html += scSmartMenuHtml(idx, null, "variable", "ifLeft");
  } else {
    html += '<button type="button" class="sc-pill sc-smart-trigger sc-smart-choice" data-smart-trigger="choice">Condition</button>';
    html += scSmartMenuHtml(idx, null, "input", "ifLeft");
  }
  html += '</span>';
  return html;
}
function scMergeTextParts(parts) {
  if (!parts || !parts.length) return [{mode: "text", text: ""}];
  var out = [];
  parts.forEach(function (p) {
    if (p.mode === "variable") {
      out.push({mode: "variable", variable: p.variable || "messageSender"});
      return;
    }
    var text = p.text == null ? "" : String(p.text);
    if (out.length && out[out.length - 1].mode === "text") out[out.length - 1].text += text;
    else out.push({mode: "text", text: text});
  });
  if (!out.length) out.push({mode: "text", text: ""});
  if (out[out.length - 1].mode === "variable") out.push({mode: "text", text: ""});
  return out;
}
function scTextBodyInputHtml(text, idx, kind) {
  var val = esc(String(text == null ? "" : text));
  return '<div class="sc-text-composer sc-text-composer-single">' +
    '<span class="sc-smart-field sc-text-body-field" data-smart-slot="textBody" data-idx="' + idx + '" data-kind="' + kind + '">' +
    '<textarea class="sc-text-body-input sc-smart-trigger" data-input="textBody" data-idx="' + idx + '" data-kind="' + kind + '" rows="2" placeholder="Text" spellcheck="false">' + val + '</textarea>' +
    scSmartMenuHtml(idx, null, "input", "textPart") +
    '</span></div>';
}
function scMessageValueHtml(data, idx, kind) {
  scNormalizeAction(data, idx);
  return scSmartFieldHtml({
    slot: "message", idx: idx, kind: kind, beforeIdx: idx,
    mode: data.messageMode === "variable" ? "variable" : "text",
    textValue: data.messageText, varValue: data.messageVariable || "messageText",
    textInputField: "messageText", placeholder: "Text"
  });
}
function scChannelValueHtml(data, idx, kind) {
  scNormalizeAction(data, idx);
  if (data.channelMode === "variable") {
    return scSmartFieldHtml({
      slot: "channel", idx: idx, kind: kind, beforeIdx: idx, mode: "variable",
      varValue: data.channelVariable || "messageChannel"
    });
  }
  return scSmartFieldHtml({
    slot: "channel", idx: idx, kind: kind, beforeIdx: idx, mode: "text",
    textValue: data.channel || "all", choice: true,
    choiceLabel: scLabelChannel(data.channel || "all"),
    fixedChoices: SC_CHANNEL_CHOICES
  });
}
function scSendChatShowsRoom(data) {
  scNormalizeAction(data);
  if (data.channelMode === "variable") return true;
  return data.channelMode === "text" && data.channel === "uclient";
}
function scUClientRoomValueHtml(data, idx, kind) {
  scNormalizeAction(data, idx);
  return scSmartFieldHtml({
    slot: "uclientRoom", idx: idx, kind: kind, beforeIdx: idx,
    mode: data.uclientRoomMode === "variable" ? "variable" : "text",
    textValue: data.uclientRoomId, varValue: data.uclientRoomVariable || "messageUClientRoomId",
    textInputField: "uclientRoomId", placeholder: "Room ID"
  });
}
function scTextBlockHtml(data, idx, kind) {
  scNormalizeAction(data, idx);
  data.parts = scMergeTextParts(data.parts);
  var hasVar = data.parts.some(function (p) { return p.mode === "variable"; });
  var bodyHtml;
  if (!hasVar) {
    bodyHtml = scTextBodyInputHtml((data.parts[0] && data.parts[0].text) || "", idx, kind);
  } else {
    var partsHtml = "";
    data.parts.forEach(function (part, pi) {
      partsHtml += scSmartFieldHtml({
        slot: "textPart", idx: idx, kind: kind, partIdx: pi, beforeIdx: idx,
        mode: part.mode === "variable" ? "variable" : "text",
        textValue: part.text, varValue: part.variable || "messageSender",
        textInputField: "textPart"
      });
    });
    bodyHtml = '<div class="sc-text-composer"><span class="sc-text-parts">' + partsHtml + '</span></div>';
  }
  return '<div class="sc-text-head"><span class="sc-text-title">Text</span></div>' + bodyHtml;
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
  $("shortcuts-view").classList.toggle("empty", !shortcutsLocal.length);
  if (!shortcutsLocal.length) {
    list.innerHTML = '<div class="sc-empty"><b>No automations yet</b>Create a When trigger to detect chat, then chain actions.</div>';
    return;
  }
  list.innerHTML = shortcutsLocal.map(function (sc) {
    var on = sc.enabled !== false;
    var trigTitle = sc.trigger ? scTriggerDef(sc.trigger.type).title : "Automation";
    var trigWhen = sc.trigger ? scTriggerWhenPreview(sc.trigger) : "";
    return '<div class="sc-row' + (on ? "" : " off") + '" data-id="' + esc(sc.id) + '">' +
      '<div class="sc-flow"><span class="sc-ico chat">\u2709</span><span class="sc-arrow">\u2192</span><span class="sc-ico action">\u2699</span></div>' +
      '<div class="sc-text"><b>' + esc(sc.name || trigTitle) + '</b><small>' + esc(trigWhen) + '</small></div>' +
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
function scOpenPop(anchor, options, onPick, currentVal) {
  scCloseAllPickers(null);
  scPopEl = document.createElement("div");
  scPopEl.className = "sc-pop";
  var html = '<div class="sc-smart-menu-inner">';
  html += options.map(function (opt) {
    var on = String(opt.value) === String(currentVal == null ? "" : currentVal);
    return '<button type="button" class="sc-smart-menu-item' + (on ? " on" : "") + (opt.isVar ? " is-var" : "") + '" data-val="' + esc(String(opt.value)) + '">' + esc(opt.label) + '</button>';
  }).join("");
  html += "</div>";
  scPopEl.innerHTML = html;
  document.body.appendChild(scPopEl);
  var r = anchor.getBoundingClientRect();
  var left = Math.min(r.left, window.innerWidth - scPopEl.offsetWidth - 8);
  scPopEl.style.left = left + "px";
  scPopEl.style.top = (r.bottom + 6) + "px";
  var originX = Math.max(12, Math.min(r.left + r.width * 0.5 - left, scPopEl.offsetWidth - 12));
  scPopEl.style.transformOrigin = originX + "px 0";
  scPopEl.addEventListener("click", function (e) {
    var btn = e.target.closest("button[data-val]");
    if (!btn) return;
    onPick(btn.dataset.val);
    scClosePop();
  });
}
document.addEventListener("click", function (e) {
  if (e.target.closest(".sc-pop") || e.target.closest(".sc-smart-menu")) return;
  if (e.target.closest(".sc-smart-trigger")) return;
  if (e.target.closest(".sc-pill[data-field]")) return;
  scCloseAllPickers(null);
});

function scFindMatchingEndIf(actions, ifIdx) {
  if (!actions || ifIdx < 0 || ifIdx >= actions.length || actions[ifIdx].type !== "if") return -1;
  var depth = 1;
  for (var i = ifIdx + 1; i < actions.length; i++) {
    if (actions[i].type === "if") depth++;
    else if (actions[i].type === "end_if") {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}
function scFindOtherwiseInIf(actions, ifIdx, endIdx) {
  if (endIdx < 0) return -1;
  var depth = 0;
  for (var i = ifIdx + 1; i < endIdx; i++) {
    var type = actions[i].type;
    if (type === "if") depth++;
    else if (type === "end_if") depth--;
    else if (type === "otherwise" && depth === 0) return i;
  }
  return -1;
}
function scIfBlockMeta(actions, ifIdx, endIdx, idx) {
  if (idx === ifIdx) return {role: "if"};
  if (idx === endIdx) return {role: "end"};
  if (actions[idx].type === "otherwise") return {role: "otherwise"};
  var otherwiseIdx = scFindOtherwiseInIf(actions, ifIdx, endIdx);
  if (otherwiseIdx < 0 || idx < otherwiseIdx) return {role: "branch", branch: "then"};
  if (idx > otherwiseIdx) return {role: "branch", branch: "else"};
  return {};
}
function scIsInsideIfGroup(idx) {
  var actions = scEditing && scEditing.actions;
  if (!actions) return false;
  for (var i = 0; i < actions.length; i++) {
    if (actions[i].type !== "if") continue;
    var endIdx = scFindMatchingEndIf(actions, i);
    if (endIdx >= 0 && idx > i && idx <= endIdx) return true;
  }
  return false;
}
function scRepairIfBlocks(actions) {
  if (!actions || !actions.length) return actions;
  for (var i = 0; i < actions.length; i++) {
    if (actions[i].type !== "if") continue;
    var endIdx = scFindMatchingEndIf(actions, i);
    if (endIdx < 0) continue;
    if (scFindOtherwiseInIf(actions, i, endIdx) < 0) {
      actions.splice(endIdx, 0, {type: "otherwise"});
    }
    // Keep walking into the block so nested ifs get repaired as well.
  }
  return actions;
}
function scRenderActionBlocks(actions, enterIdx) {
  var html = "";
  var i = 0;
  while (i < actions.length) {
    var act = scNormalizeAction(actions[i], i);
    if (act.type === "if") {
      var endIdx = scFindMatchingEndIf(actions, i);
      if (endIdx < 0) {
        html += scBlockHtml("action", act, i, i === enterIdx);
        i++;
        continue;
      }
      var groupEnter = enterIdx >= i && enterIdx <= endIdx;
      html += '<div class="sc-if-group' + (groupEnter ? " sc-enter" : "") + '">';
      for (var j = i; j <= endIdx; j++) {
        html += scBlockHtml("action", scNormalizeAction(actions[j], j), j, j === enterIdx,
          scIfBlockMeta(actions, i, endIdx, j));
      }
      html += '</div>';
      i = endIdx + 1;
    } else {
      html += scBlockHtml("action", act, i, i === enterIdx);
      i++;
    }
  }
  return html;
}

/* -- test run progress ------------------------------------------------- */
var scRun = null;
function scRunActive() {
  return !!(scRun && scRun.status === "running");
}
var SC_PLAY_ICON = '<svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path fill="currentColor" d="M8.05 5.55a1.35 1.35 0 0 1 2.09-1.12l8.31 5.54a1.35 1.35 0 0 1 0 2.24l-8.31 5.54a1.35 1.35 0 0 1-2.09-1.12V5.55z"/></svg>';
function scUpdatePlayButton() {
  var btn = $("sc-tb-play");
  if (!btn) return;
  var running = scRunActive();
  // The icon stays the play glyph while running, only the action flips to stop.
  if (!btn.firstChild) btn.innerHTML = SC_PLAY_ICON;
  btn.title = running ? "Stop test run" : "Test run";
  btn.disabled = !running && !(lastState && lastState.gameRunning);
}
var SC_WAIT_SECONDS_MIN = 1;
var SC_WAIT_SECONDS_MAX = 3600;
function scClampWaitSeconds(raw) {
  var digits = String(raw == null ? "" : raw).replace(/[^0-9]/g, "");
  var n = parseInt(digits, 10);
  if (!isFinite(n) || n < SC_WAIT_SECONDS_MIN) n = SC_WAIT_SECONDS_MIN;
  if (n > SC_WAIT_SECONDS_MAX) n = SC_WAIT_SECONDS_MAX;
  return n;
}
function scRunWaitTotal(idx) {
  var act = scEditing && scEditing.actions ? scEditing.actions[idx] : null;
  var seconds = act && act.type === "wait" ? Number(act.seconds) : 0;
  return seconds > 0 ? seconds : 0;
}
function scApplyRunWait(block, idx, remaining) {
  var el = block.querySelector(".sc-run-wait-sweep");
  if (remaining == null) {
    if (el) el.remove();
    block.classList.remove("sc-run-waiting");
    return;
  }
  if (!el) {
    el = document.createElement("span");
    el.className = "sc-run-wait-sweep";
    block.insertBefore(el, block.firstChild);
  }
  block.classList.add("sc-run-waiting");
  var total = scRunWaitTotal(idx);
  var left = Math.max(0, Number(remaining) || 0);
  var pct = total > 0 ? (1 - left / total) * 100 : 100;
  el.style.width = Math.max(0, Math.min(100, pct)).toFixed(2) + "%";
}
function scRunReflowTargets(canvas) {
  return canvas.querySelectorAll(".sc-block, .sc-if-group, .sc-run-result");
}
function scRunFlipLayout(canvas, mutateFn) {
  var items = scRunReflowTargets(canvas);
  var tops = new Map();
  items.forEach(function (el) { tops.set(el, el.getBoundingClientRect().top); });
  mutateFn();
  canvas.classList.add("sc-run-reflow");
  items.forEach(function (el) {
    var prev = tops.get(el);
    if (prev == null) return;
    var dy = prev - el.getBoundingClientRect().top;
    if (Math.abs(dy) < 0.5) return;
    el.style.transform = "translateY(" + dy.toFixed(2) + "px)";
    el.style.transition = "none";
    requestAnimationFrame(function () {
      requestAnimationFrame(function () {
        el.style.transition = "";
        el.style.transform = "";
      });
    });
  });
  clearTimeout(scRunFlipLayout._t);
  scRunFlipLayout._t = setTimeout(function () { canvas.classList.remove("sc-run-reflow"); }, 450);
}
function scApplyRunResults(canvas) {
  var results = scRun && scRun.results ? scRun.results : [];
  var keep = Object.create(null);
  var inserts = [];
  var toRemove = [];
  results.forEach(function (r) {
    if (!r || r.index == null) return;
    var idx = Number(r.index);
    var block = canvas.querySelector('.sc-block[data-block-kind="action"][data-block-idx="' + idx + '"]');
    if (!block) return;
    keep[idx] = true;
    var el = canvas.querySelector('.sc-run-result[data-run-result="' + idx + '"]');
    if (!el) {
      inserts.push({idx: idx, block: block, value: r.value});
      return;
    }
    var box = el.querySelector(".sc-run-result-box");
    if (!box) return;
    var val = r.value == null ? "" : String(r.value);
    if (val) box.textContent = val;
    else box.innerHTML = "<em>No value</em>";
  });
  canvas.querySelectorAll(".sc-run-result").forEach(function (el) {
    if (!keep[Number(el.getAttribute("data-run-result"))]) toRemove.push(el);
  });
  if (!inserts.length && !toRemove.length) return;
  scRunFlipLayout(canvas, function () {
    inserts.forEach(function (ins) {
      var row = document.createElement("div");
      row.className = "sc-run-result sc-enter";
      row.setAttribute("data-run-result", String(ins.idx));
      var val = ins.value == null ? "" : String(ins.value);
      row.innerHTML = '<div class="sc-run-result-box">' + (val ? esc(val) : "<em>No value</em>") + "</div>";
      ins.block.insertAdjacentElement("afterend", row);
    });
    toRemove.forEach(function (el) { el.remove(); });
  });
}
function scApplyRunState() {
  var canvas = $("sc-ed-canvas");
  if (!canvas) return;
  var step = scRun && scRun.step != null ? scRun.step : -1;
  canvas.querySelectorAll('.sc-block[data-block-kind="action"]').forEach(function (block) {
    var idx = Number(block.dataset.blockIdx);
    var current = scRunActive() && idx === step;
    block.classList.toggle("sc-run-current", current);
    scApplyRunWait(block, idx, current && scRun.waitRemaining != null ? scRun.waitRemaining : null);
  });
  scApplyRunResults(canvas);
}
function scStopRun(notifyHost) {
  if (notifyHost && scRun && scRun.runId) send({cmd: "automationRunStop", runId: scRun.runId});
  scRun = null;
  scApplyRunState();
  scUpdatePlayButton();
}
window.__setAutomationRun = function (st) {
  if (!st || !scRun || st.runId !== scRun.runId) return;
  scRun.status = st.status === "done" ? "done" : "running";
  scRun.step = st.status === "done" || st.step == null ? -1 : Number(st.step);
  scRun.total = st.total == null ? scRun.total : Number(st.total);
  scRun.waitRemaining = st.status === "done" || st.waitRemaining == null ? null : Number(st.waitRemaining);
  scRun.results = st.results || [];
  scApplyRunState();
  scUpdatePlayButton();
};

var SC_HISTORY_LIMIT = 50;
var scEditHistory = [];
var scEditFuture = [];
var scHistoryApplying = false;
var scTitleDirty = false;
function scEditingSnapshot() {
  return scEditing ? JSON.stringify(scEditing) : null;
}
function scUpdateHistoryButtons() {
  var undo = $("sc-tb-undo");
  var redo = $("sc-tb-redo");
  if (undo) undo.disabled = !scEditHistory.length;
  if (redo) redo.disabled = !scEditFuture.length;
}
function scResetHistory() {
  scEditHistory = [];
  scEditFuture = [];
  scTitleDirty = false;
  scUpdateHistoryButtons();
}
function scPushHistory() {
  if (scHistoryApplying || !scEditing) return;
  var snap = scEditingSnapshot();
  if (scEditHistory.length && scEditHistory[scEditHistory.length - 1] === snap) return;
  scEditHistory.push(snap);
  if (scEditHistory.length > SC_HISTORY_LIMIT) scEditHistory.shift();
  scEditFuture = [];
  scUpdateHistoryButtons();
}
function scApplyHistorySnapshot(snap) {
  scHistoryApplying = true;
  scEditing = JSON.parse(snap);
  scCloseAllPickers(null);
  scRenderEditor();
  scHistoryApplying = false;
  scTitleDirty = false;
  scUpdateHistoryButtons();
}
function scUndo() {
  if (!scEditing || !scEditHistory.length) return;
  scEditFuture.push(scEditingSnapshot());
  if (scEditFuture.length > SC_HISTORY_LIMIT) scEditFuture.shift();
  scApplyHistorySnapshot(scEditHistory.pop());
}
function scRedo() {
  if (!scEditing || !scEditFuture.length) return;
  scEditHistory.push(scEditingSnapshot());
  if (scEditHistory.length > SC_HISTORY_LIMIT) scEditHistory.shift();
  scApplyHistorySnapshot(scEditFuture.pop());
}
function scRenderEditor(opts) {
  var canvas = $("sc-ed-canvas");
  if (!scEditing) { canvas.innerHTML = ""; return; }
  if (scEditing.trigger) scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  var scroll = canvas.scrollTop;
  var enterIdx = opts && opts.enterActionIdx != null ? opts.enterActionIdx : -1;
  $("sc-ed-title").value = scEditing.name || "New Shortcut";
  var html = "";
  if (scEditing.trigger) {
    html += scBlockHtml("trigger", scEditing.trigger, 0, enterIdx === -2);
  }
  html += scRenderActionBlocks(scEditing.actions || [], enterIdx);
  canvas.innerHTML = html || '<div class="sc-ed-empty"><b>Start with When</b>Pick a When trigger below to detect chat events, then add actions.</div>';
  canvas.scrollTop = scroll;
  scSyncPillInputs(canvas);
  scBindCanvasDropGap(null);
  scApplyRunState();
}
function scGetIfMetaForIndex(idx) {
  var actions = scEditing && scEditing.actions;
  if (!actions) return {};
  for (var i = 0; i < actions.length; i++) {
    if (actions[i].type !== "if") continue;
    var endIdx = scFindMatchingEndIf(actions, i);
    if (endIdx < 0) continue;
    if (idx >= i && idx <= endIdx) return scIfBlockMeta(actions, i, endIdx, idx);
  }
  return {};
}
function scUpdateIfGroupBlock(idx) {
  if (!scEditing) return;
  var canvas = $("sc-ed-canvas");
  var el = canvas.querySelector('.sc-block[data-block-kind="action"][data-block-idx="' + idx + '"]');
  if (!el) {
    scRenderEditor();
    return;
  }
  var data = scNormalizeAction(scEditing.actions[idx], idx);
  var wrap = document.createElement("div");
  wrap.innerHTML = scBlockHtml("action", data, idx, false, scGetIfMetaForIndex(idx));
  var fresh = wrap.firstElementChild;
  if (fresh) el.replaceWith(fresh);
  scSyncPillInputs(canvas);
  scApplyRunState();
}
function scUpdateBlockElement(kind, idx) {
  if (!scEditing) return;
  if (kind === "trigger" && scEditing.trigger) scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  if (kind === "action") {
    var act = scEditing.actions[idx];
    if (act && (act.type === "if" || act.type === "otherwise" || act.type === "end_if" || scIsInsideIfGroup(idx))) {
      scUpdateIfGroupBlock(idx);
      return;
    }
  }
  var canvas = $("sc-ed-canvas");
  var sel = kind === "trigger"
    ? '.sc-block[data-block-kind="trigger"]'
    : '.sc-block[data-block-kind="action"][data-block-idx="' + idx + '"]';
  var el = canvas.querySelector(sel);
  if (!el) { scRenderEditor(); return; }
  var data = kind === "trigger" ? scEditing.trigger : scEditing.actions[idx];
  var wrap = document.createElement("div");
  wrap.innerHTML = scBlockHtml(kind, data, idx, false);
  var fresh = wrap.firstElementChild;
  if (fresh) el.replaceWith(fresh);
  scSyncPillInputs(canvas);
  scApplyRunState();
}
function scAppendBlockElement(kind, idx) {
  if (!scEditing) return;
  var canvas = $("sc-ed-canvas");
  var empty = canvas.querySelector(".sc-ed-empty");
  if (empty) empty.remove();
  var data = kind === "trigger" ? scEditing.trigger : scEditing.actions[idx];
  var wrap = document.createElement("div");
  wrap.innerHTML = scBlockHtml(kind, data, idx, true);
  var block = wrap.firstElementChild;
  if (!block) return;
  if (kind === "trigger") canvas.insertBefore(block, canvas.firstChild);
  else canvas.appendChild(block);
  scSyncPillInputs(block);
}
function scRemoveActionBlock(idx) {
  if (!scEditing || !scEditing.actions) return;
  var act = scEditing.actions[idx];
  if (!act) return;
  if (act.type === "end_if") return;
  scPushHistory();
  if (act.type === "if") {
    var endIdx = scFindMatchingEndIf(scEditing.actions, idx);
    if (endIdx >= 0) scEditing.actions.splice(idx, endIdx - idx + 1);
    else scEditing.actions.splice(idx, 1);
  } else {
    scEditing.actions.splice(idx, 1);
  }
  if (!scEditing.actions.length && !scEditing.trigger) {
    $("sc-ed-canvas").innerHTML = '<div class="sc-ed-empty"><b>Start with When</b>Pick a When trigger below to detect chat events, then add actions.</div>';
  } else {
    scRenderEditor();
  }
}
function scBlockGrip(kind, idx, actType) {
  if (kind === "action") {
    if (actType === "otherwise" || actType === "end_if") {
      return '<span class="sc-block-grip spacer" aria-hidden="true"></span>';
    }
    return '<button type="button" class="sc-block-grip" data-drag-kind="action" data-drag-idx="' + idx + '" aria-label="Reorder"><span></span><span></span><span></span></button>';
  }
  return '<span class="sc-block-grip spacer" aria-hidden="true"></span>';
}
function scServerTargetsHtml(trigger) {
  if (!trigger.targets || !Array.isArray(trigger.targets)) trigger.targets = [];
  var targets = trigger.targets;
  var hasDraft = targets.length > 0 && targets[targets.length - 1] === "";
  var showInput = !targets.length || hasDraft;
  var filled = targets.filter(function (n) { return n.length > 0; });
  var html = '<div class="sc-block-filter sc-block-server-targets"><span class="sc-sender-names">';
  for (var i = 0; i < targets.length; i++) {
    var n = targets[i];
    if (n) {
      html += '<span class="sc-sender-chip">' +
        '<span class="sc-sender-chip-label">' + esc(n) + '</span>' +
        '</span>';
    } else if (i === targets.length - 1) {
      html += scPillInput("serverTarget", "", 0, "trigger", i);
    }
  }
  if (!targets.length) {
    html += scPillInput("serverTarget", "", 0, "trigger", 0);
    showInput = true;
  }
  if (!showInput && filled.length > 0) {
    html += '<button type="button" class="sc-sender-add" data-add-server-target aria-label="Add server">+</button>';
  }
  html += '</span></div>';
  return html;
}
function scSenderNamesHtml(filter, filterIdx) {
  if (!filter.names || !Array.isArray(filter.names)) filter.names = [];
  var names = filter.names;
  var hasDraft = names.length > 0 && names[names.length - 1] === "";
  var showInput = !names.length || hasDraft;
  var filled = names.filter(function (n) { return n.length > 0; });
  var html = '<span class="sc-sender-names">';
  for (var i = 0; i < names.length; i++) {
    var n = names[i];
    if (n) {
      html += '<span class="sc-sender-chip">' +
        '<span class="sc-sender-chip-label">' + esc(n) + '</span>' +
        '</span>';
    } else if (i === names.length - 1) {
      html += scPillInput("senderName", "", filterIdx, "trigger-filter", i);
    }
  }
  if (!names.length) {
    html += scPillInput("senderName", "", filterIdx, "trigger-filter", 0);
    showInput = true;
  }
  if (!showInput && filled.length > 0) {
    html += '<button type="button" class="sc-sender-add" data-add-sender-name="' + filterIdx + '" aria-label="Add player">+</button>';
  }
  html += '</span>';
  return html;
}
function scTriggerFilterRowHtml(filter, filterIdx) {
  var opTxt = filter.kind === "message" ? "contains" : "is";
  var valueHtml = "";
  if (filter.kind === "sender") {
    valueHtml = scSenderNamesHtml(filter, filterIdx);
  } else if (filter.kind === "chat_channel") {
    valueHtml = scPill("channel", filter.channel || "all", filterIdx, "trigger-filter");
  } else if (filter.kind === "uclient_room") {
    valueHtml = scPillInput("room", filter.room, filterIdx, "trigger-filter");
  } else {
    valueHtml = scPillInput("text", filter.text, filterIdx, "trigger-filter");
  }
  var removeBtn = filterIdx > 0
    ? '<button type="button" class="sc-filter-del" data-remove-filter="' + filterIdx + '" aria-label="Remove filter">\u2212</button>'
    : "";
  return '<div class="sc-block-filter" data-filter-idx="' + filterIdx + '">' +
    scPill("kind", filter.kind, filterIdx, "trigger-filter") +
    scTxt(opTxt) + valueHtml + removeBtn + '</div>';
}
function scTriggerBlockHtml(data, enter) {
  data = scNormalizeTrigger(JSON.parse(JSON.stringify(data)));
  var enterCls = enter ? " sc-enter" : "";
  var tdef = scTriggerDef(data.type);
  var filtersHtml = data.filters.map(function (f, i) {
    return scTriggerFilterRowHtml(f, i);
  }).join("");
  var addBtn = data.filters.length < 5
    ? '<button type="button" class="sc-filter-add" data-add-filter>+ Add filter</button>'
    : "";
  return '<div class="sc-block sc-block-trigger' + enterCls + '" data-block-kind="trigger" data-block-idx="0">' +
    '<div class="sc-block-trigger-main">' +
    '<div class="sc-block-trigger-head">' +
    '<span class="sc-block-ico ' + esc(tdef.tone || "chat") + '">' + scBlockIconInner(tdef.tone || "chat", tdef.icon) + '</span>' +
    '<span class="sc-block-trigger-title">When a message matching the following conditions is received</span>' +
    '</div>' +
    '<div class="sc-block-trigger-divider"></div>' +
    '<div class="sc-block-filters">' + filtersHtml + '</div>' +
    addBtn +
    '</div></div>';
}
function scServerConnectBlockHtml(data, enter) {
  data = scNormalizeTrigger(JSON.parse(JSON.stringify(data)));
  var enterCls = enter ? " sc-enter" : "";
  var tdef = scTriggerDef(data.type);
  return '<div class="sc-block sc-block-trigger' + enterCls + '" data-block-kind="trigger" data-block-idx="0">' +
    '<div class="sc-block-trigger-main">' +
    '<div class="sc-block-trigger-head">' +
    '<span class="sc-block-ico ' + esc(tdef.tone || "connect") + '">' + scBlockIconInner(tdef.tone || "connect", tdef.icon) + '</span>' +
    '<span class="sc-block-trigger-title">When connecting to one of the following servers</span>' +
    '</div>' +
    '<div class="sc-block-trigger-divider"></div>' +
    scServerTargetsHtml(data) +
    '</div></div>';
}
function scBlockHtml(kind, data, idx, enter, ifMeta) {
  ifMeta = ifMeta || {};
  var canDel = kind === "action" && data.type !== "end_if";
  var del = canDel ? '<button class="sc-del" type="button" data-del-action="' + idx + '" aria-label="Remove">' + scIconX() + '</button>' : "";
  var enterCls = enter ? " sc-enter" : "";
  var ifCls = "";
  if (ifMeta.role === "branch") ifCls = " sc-if-branch sc-if-branch-" + ifMeta.branch;
  else if (ifMeta.role === "if") ifCls = " sc-if-head";
  else if (ifMeta.role === "otherwise") ifCls = " sc-if-otherwise";
  else if (ifMeta.role === "end") ifCls = " sc-if-foot";
  var roleAttr = ifMeta.role ? ' data-if-role="' + esc(ifMeta.role) + '"' : "";
  var branchAttr = ifMeta.branch ? ' data-if-branch="' + esc(ifMeta.branch) + '"' : "";
  var inner = "";
  if (kind === "trigger" && data.type === "server_connect") {
    return scServerConnectBlockHtml(data, enter);
  }
  if (kind === "trigger" && data.type === "chat_received") {
    return scTriggerBlockHtml(data, enter);
  }
  if (data.type === "send_chat") {
    inner = scTxt("Send") + scMessageValueHtml(data, idx, kind) + scTxt("to") +
      scChannelValueHtml(data, idx, kind) + scTxt("chat");
    if (scSendChatShowsRoom(data)) {
      inner += scTxt("in") + scUClientRoomValueHtml(data, idx, kind);
    }
  } else if (data.type === "text") {
    var adefText = scActionDef(data.type);
    return '<div class="sc-block sc-block-action sc-block-text' + enterCls + ifCls + '" data-block-kind="action" data-block-idx="' + idx + '"' + roleAttr + branchAttr + '>' +
      scBlockGrip(kind, idx, data.type) +
      '<span class="sc-block-ico ' + esc(adefText.tone || "text") + '">' + scBlockIconInner(adefText.tone || "text", adefText.icon) + '</span>' +
      '<div class="sc-block-body">' + scTextBlockHtml(data, idx, kind) + '</div>' + del + '</div>';
  } else if (data.type === "connect_server") {
    inner = scTxt("Connect to") + scPillInput("address", data.address, idx, kind);
  } else if (data.type === "leave_server") {
    inner = scTxt("Leave server");
  } else if (data.type === "wait") {
    inner = scTxt("Wait") + scPillInput("seconds", data.seconds, idx, kind) + scTxt("seconds");
  } else if (data.type === "switch_weapon_use") {
    inner = scTxt("Switch to") + scPill("weapon", data.weapon, idx, kind) + scTxt("and use");
  } else if (data.type === "set_skin") {
    inner = scTxt("Set") + scPill("target", data.target, idx, kind) + scTxt("skin to") +
      scPillInput("skin", data.skin, idx, kind);
  } else if (data.type === "set_custom_color") {
    inner = scTxt("Set") + scPill("target", data.target, idx, kind) + scTxt("custom colors") +
      scPill("enabled", data.enabled ? "on" : "off", idx, kind);
  } else if (data.type === "set_body_color" || data.type === "set_feet_color") {
    var part = data.type === "set_body_color" ? "body" : "feet";
    inner = scTxt("Set") + scPill("target", data.target, idx, kind) + scTxt(part + " color") +
      scPillInput("color", data.color, idx, kind);
  } else if (data.type === "set_name") {
    inner = scTxt("Set") + scPill("target", data.target, idx, kind) + scTxt("name") +
      scPillInput("name", data.name, idx, kind);
  } else if (data.type === "get") {
    inner = scTxt("Get") + scPill("property", data.property, idx, kind);
  } else if (data.type === "if") {
    scNormalizeIfCondition(data);
    inner = scTxt("If") + scIfLeftFieldHtml(data, idx, kind);
    if (data.left) {
      inner += scPill("op", data.op, idx, kind);
      if (scIfOpNeedsRight(data.op)) {
        if (scIfLeftValueKind(data.left) === "channel") {
          inner += scPill("right", data.right || "all", idx, kind);
        } else {
          inner += scPillInput("right", data.right, idx, kind);
        }
      } else {
        inner += scTxt("value");
      }
    }
  } else if (data.type === "otherwise") {
    inner = scTxt("Otherwise");
  } else if (data.type === "end_if") {
    inner = scTxt("End If");
  } else if (data.type === "stop") {
    inner = scTxt("Stop");
  }
  if (kind === "trigger") {
    return scTriggerBlockHtml(data, enter);
  }
  var adef = scActionDef(data.type);
  return '<div class="sc-block sc-block-action' + enterCls + ifCls + '" data-block-kind="action" data-block-idx="' + idx + '"' + roleAttr + branchAttr + '>' +
    scBlockGrip(kind, idx, data.type) +
    '<span class="sc-block-ico ' + esc(adef.tone || "action") + '">' + scBlockIconInner(adef.tone || "action", adef.icon) + '</span>' +
    '<div class="sc-block-body"><div class="sc-block-line">' + inner + '</div></div>' + del + '</div>';
}
function scTxt(text) {
  return '<span class="sc-txt">' + esc(text) + '</span>';
}
function scPill(field, value, idx, kind) {
  var label = String(value || "");
  if (field === "kind") {
    if (value === "message") label = "Message";
    else if (value === "chat_channel") label = "Chat channel";
    else if (value === "uclient_room") label = "UClient room";
    else label = "Sender";
  }
  if (field === "channel") label = scLabelChannel(value);
  if (field === "match") label = value === "equals" ? "equals" : value === "starts_with" ? "starts with" : "contains";
  if (field === "weapon") label = scLabelWeapon(value);
  if (field === "target") label = value === "dummy" ? "Dummy" : "Player";
  if (field === "enabled") label = value === "on" || value === true ? "On" : "Off";
  if (field === "property") label = scLabelGetProperty(value);
  if (field === "left") label = value ? (scLabelVariable(value) || scLabelGetProperty(value)) : "Condition";
  if (field === "op") label = scLabelIfOp(value);
  if (field === "right") label = String(value || "");
  var isVar = field === "messageVariable" || field === "textPartVar" ||
    field === "channelVariable" || field === "uclientRoomVariable";
  if (isVar) {
    return scVarPillButton("", value, label, ' data-field="' + esc(field) + '" data-idx="' + idx + '" data-kind="' + kind + '"');
  }
  return '<button type="button" class="sc-pill" data-field="' + esc(field) + '" data-idx="' + idx + '" data-kind="' + kind + '">' + esc(label) + '</button>';
}
function scPillInputPlaceholder(field) {
  if (field === "text" || field === "message" || field === "messageText" || field === "textPart") return "Text";
  if (field === "senderName") return "Sender";
  if (field === "serverTarget") return "IP or IP:port";
  if (field === "address") return "IP or IP:port";
  if (field === "uclientRoomId") return "Room ID (empty = global)";
  if (field === "room") return "Room ID or name";
  if (field === "right") return "Text";
  return "";
}
function scPillInput(field, value, idx, kind, subIdx, extraClass) {
  var val = esc(String(value == null ? "" : value));
  var mode = field === "seconds" ? ' inputmode="numeric"' : (field === "color" ? ' inputmode="decimal"' : "");
  var ph = scPillInputPlaceholder(field);
  var phAttr = ph ? ' placeholder="' + esc(ph) + '"' : "";
  var subAttr = subIdx != null ? ' data-name-idx="' + subIdx + '"' : "";
  var cls = "sc-pill input" + (extraClass ? " " + extraClass : "");
  return '<input type="text" class="' + cls + '" data-input="' + esc(field) + '" data-idx="' + idx + '" data-kind="' + kind + '" value="' + val + '" spellcheck="false" size="1"' + mode + phAttr + subAttr + ' data-smart-trigger="input">';
}
var scPillMeasureNode = null;
var SC_PILL_INPUT_MIN = 26;
var SC_PILL_INPUT_MAX = 240;
function scPillMeasureStyle(input) {
  if (!scPillMeasureNode) {
    scPillMeasureNode = document.createElement("span");
    scPillMeasureNode.className = "sc-pill-measure";
    document.body.appendChild(scPillMeasureNode);
  }
  var cs = getComputedStyle(input);
  scPillMeasureNode.style.font = cs.font;
  scPillMeasureNode.style.letterSpacing = cs.letterSpacing;
  scPillMeasureNode.style.fontWeight = cs.fontWeight;
  return scPillMeasureNode;
}
function scMeasurePillInput(input, text) {
  var m = scPillMeasureStyle(input);
  m.textContent = text.length ? text : " ";
  return m.getBoundingClientRect().width;
}
function scFitPillInput(input) {
  if (!input) return;
  var val = String(input.value || "");
  var focused = document.activeElement === input;
  var ph = val.length ? "" : String(input.getAttribute("placeholder") || "");
  var textW = scMeasurePillInput(input, val.length ? val : (ph.length ? ph : " "));
  var cs = getComputedStyle(input);
  var padX = (parseFloat(cs.paddingLeft) || 0) + (parseFloat(cs.paddingRight) || 0);
  var fullW = Math.ceil(textW + padX);
  var minW = SC_PILL_INPUT_MIN;
  var maxW = SC_PILL_INPUT_MAX;
  if (focused) {
    input.classList.remove("is-truncated");
    input.removeAttribute("title");
    input.style.width = Math.max(minW, Math.min(fullW, maxW)) + "px";
    return;
  }
  if (fullW > maxW) {
    input.classList.add("is-truncated");
    input.removeAttribute("title");
    input.style.width = maxW + "px";
    return;
  }
  input.classList.remove("is-truncated");
  input.removeAttribute("title");
  input.style.width = Math.max(minW, fullW) + "px";
}
function scSyncPillInputs(root) {
  (root || $("sc-ed-canvas")).querySelectorAll("input.sc-pill.input").forEach(scFitPillInput);
}
function scCommitAllPillInputs(root) {
  var el = root || $("sc-ed-canvas");
  el.querySelectorAll("input.sc-pill.input").forEach(function (input) {
    scCommitPillInput(input, false);
  });
  el.querySelectorAll("textarea.sc-text-body-input").forEach(function (input) {
    scCommitPillInput(input, false);
  });
}
function scCommitPillInput(input, revert) {
  if (!input || !scEditing) return;
  var field = input.dataset.input;
  var kind = input.dataset.kind;
  var idx = Number(input.dataset.idx || 0);
  if (revert) {
    var orig = input.dataset.scOrig;
    if (orig != null) input.value = orig;
    scFitPillInput(input);
    return;
  }
  if (field === "senderName" && kind === "trigger-filter") {
    scCommitSenderNameInput(idx, Number(input.dataset.nameIdx || 0), input.value);
    return;
  }
  if (field === "serverTarget" && kind === "trigger") {
    scCommitServerTargetInput(Number(input.dataset.nameIdx || 0), input.value);
    return;
  }
  var ref = scGetBlockRef(kind, idx);
  if (!ref) return;
  var val = input.value;
  if (field === "seconds") {
    var secs = scClampWaitSeconds(val);
    ref.seconds = secs;
    input.value = String(secs);
  } else if (field === "color") {
    var num = Math.round(Number(val) || 0);
    num = Math.max(0, Math.min(0xFFFFFF, num));
    ref.color = num;
    input.value = String(num);
  } else if (field === "messageText") {
    scNormalizeAction(ref, idx);
    ref.messageMode = "text";
    ref.messageText = val;
  } else if (field === "uclientRoomId") {
    scNormalizeAction(ref, idx);
    ref.uclientRoomMode = "text";
    ref.uclientRoomId = val;
  } else if (field === "room") {
    ref.room = val;
  } else if (field === "textPart") {
    scNormalizeAction(ref, idx);
    var pi = Number(input.dataset.nameIdx || 0);
    if (!ref.parts[pi]) ref.parts[pi] = {mode: "text", text: ""};
    ref.parts[pi] = {mode: "text", text: val};
    ref.parts = scMergeTextParts(ref.parts);
  } else if (field === "textBody") {
    scNormalizeAction(ref, idx);
    ref.parts = [{mode: "text", text: val}];
  } else {
    ref[field] = val;
  }
  scFitPillInput(input);
  if (kind === "trigger-filter") scRememberFilterDraft(idx);
}
function scCommitSenderNameInput(filterIdx, nameIdx, rawVal) {
  var f = scGetFilterRef(filterIdx);
  if (!f || f.kind !== "sender") return;
  if (!f.names) f.names = [];
  var val = String(rawVal == null ? "" : rawVal);
  if (!val) {
    if (nameIdx > 0) {
      f.names.splice(nameIdx, 1);
      while (f.names.length > 1 && f.names[f.names.length - 1] === "") f.names.pop();
      scRememberFilterDraft(filterIdx);
      scUpdateBlockElement("trigger", 0);
    }
    return;
  }
  while (f.names.length <= nameIdx) f.names.push("");
  f.names[nameIdx] = val;
  while (f.names.length > 1 && f.names[f.names.length - 1] === "") f.names.pop();
  scRememberFilterDraft(filterIdx);
  scUpdateBlockElement("trigger", 0);
}
function scAddSenderNameSlot(filterIdx) {
  var f = scGetFilterRef(filterIdx);
  if (!f || f.kind !== "sender") return;
  if (!f.names) f.names = [];
  if (f.names.length && f.names[f.names.length - 1] === "") return;
  scPushHistory();
  f.names.push("");
  scUpdateBlockElement("trigger", 0);
  requestAnimationFrame(function () {
    var row = $("sc-ed-canvas").querySelector('.sc-block-filter[data-filter-idx="' + filterIdx + '"]');
    if (!row) return;
    var inputs = row.querySelectorAll('input[data-input="senderName"]');
    var inp = inputs[inputs.length - 1];
    if (inp) inp.focus();
  });
}
function scHandleMultiValueBackspace(input) {
  if (!input || input.value) return false;
  var field = input.dataset.input;
  if (field !== "senderName" && field !== "serverTarget") return false;
  var nameIdx = Number(input.dataset.nameIdx || 0);
  if (nameIdx <= 0) return false;
  var list = null;
  var filterIdx = 0;
  if (field === "senderName") {
    filterIdx = Number(input.dataset.idx || 0);
    var f = scGetFilterRef(filterIdx);
    if (!f || f.kind !== "sender" || !f.names) return false;
    list = f.names;
  } else {
    if (!scEditing || !scEditing.trigger || scEditing.trigger.type !== "server_connect") return false;
    scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
    if (!scEditing.trigger.targets) return false;
    list = scEditing.trigger.targets;
  }
  if (nameIdx >= list.length || list[nameIdx] !== "") return false;
  scPushHistory();
  if (list[nameIdx - 1]) {
    list.splice(nameIdx - 1, 1);
  } else {
    list.splice(nameIdx, 1);
  }
  while (list.length > 1 && list[list.length - 1] === "") list.pop();
  if (field === "senderName") scRememberFilterDraft(filterIdx);
  scUpdateBlockElement("trigger", 0);
  return true;
}
function scCommitServerTargetInput(nameIdx, rawVal) {
  if (!scEditing || !scEditing.trigger || scEditing.trigger.type !== "server_connect") return;
  scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  if (!scEditing.trigger.targets) scEditing.trigger.targets = [];
  var val = String(rawVal == null ? "" : rawVal);
  if (!val) {
    if (nameIdx > 0) {
      scEditing.trigger.targets.splice(nameIdx, 1);
      while (scEditing.trigger.targets.length > 1 && scEditing.trigger.targets[scEditing.trigger.targets.length - 1] === "") scEditing.trigger.targets.pop();
      scUpdateBlockElement("trigger", 0);
    }
    return;
  }
  while (scEditing.trigger.targets.length <= nameIdx) scEditing.trigger.targets.push("");
  scEditing.trigger.targets[nameIdx] = val;
  while (scEditing.trigger.targets.length > 1 && scEditing.trigger.targets[scEditing.trigger.targets.length - 1] === "") scEditing.trigger.targets.pop();
  scUpdateBlockElement("trigger", 0);
}
function scAddServerTargetSlot() {
  if (!scEditing || !scEditing.trigger || scEditing.trigger.type !== "server_connect") return;
  scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  if (!scEditing.trigger.targets) scEditing.trigger.targets = [];
  if (scEditing.trigger.targets.length && scEditing.trigger.targets[scEditing.trigger.targets.length - 1] === "") return;
  scPushHistory();
  scEditing.trigger.targets.push("");
  scUpdateBlockElement("trigger", 0);
  requestAnimationFrame(function () {
    var row = $("sc-ed-canvas").querySelector(".sc-block-server-targets");
    if (!row) return;
    var inputs = row.querySelectorAll('input[data-input="serverTarget"]');
    var inp = inputs[inputs.length - 1];
    if (inp) inp.focus();
  });
}
function scGetFilterRef(filterIdx) {
  if (!scEditing || !scEditing.trigger) return null;
  scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  return scEditing.trigger.filters[filterIdx] || null;
}
function scSnapshotFilter(f) {
  if (!f) return null;
  if (f.kind === "message") {
    return {kind: "message", match: f.match || "contains", text: f.text == null ? "" : String(f.text)};
  }
  return {kind: "sender", names: (f.names || []).map(function (n) { return n == null ? "" : String(n); })};
}
function scEnsureFilterDrafts() {
  if (!scEditing) return;
  if (!scEditing._filterDrafts) scEditing._filterDrafts = {};
}
function scRememberFilterDraft(filterIdx) {
  if (!scEditing || !scEditing.trigger) return;
  var f = scEditing.trigger.filters[filterIdx];
  if (!f || !f.kind) return;
  scEnsureFilterDrafts();
  if (!scEditing._filterDrafts[filterIdx]) scEditing._filterDrafts[filterIdx] = {};
  scEditing._filterDrafts[filterIdx][f.kind] = scSnapshotFilter(f);
}
function scCommitFilterRowInputs(filterIdx) {
  var row = $("sc-ed-canvas").querySelector('.sc-block-filter[data-filter-idx="' + filterIdx + '"]');
  if (!row) return;
  row.querySelectorAll("input.sc-pill.input").forEach(function (input) {
    scCommitPillInput(input, false);
  });
}
function scReindexFilterDrafts(removedIdx) {
  if (!scEditing || !scEditing._filterDrafts) return;
  var next = {};
  Object.keys(scEditing._filterDrafts).forEach(function (key) {
    var i = Number(key);
    if (i < removedIdx) next[i] = scEditing._filterDrafts[i];
    else if (i > removedIdx) next[i - 1] = scEditing._filterDrafts[i];
  });
  scEditing._filterDrafts = next;
}
function scDefaultFilter(newKind) {
  if (newKind === "message") return {kind: "message", match: "contains", text: ""};
  if (newKind === "chat_channel") return {kind: "chat_channel", channel: "all"};
  if (newKind === "uclient_room") return {kind: "uclient_room", room: ""};
  return {kind: "sender", names: []};
}
function scSeedFilterDrafts() {
  if (!scEditing || !scEditing.trigger) return;
  scEditing._filterDrafts = {};
  scEditing.trigger.filters.forEach(function (_, i) {
    scRememberFilterDraft(i);
  });
}
function scFilterFieldOptions(filterIdx) {
  if (!scEditing || !scEditing.trigger) return [];
  var t = scPrepareTriggerForEdit(scEditing.trigger);
  var used = {};
  t.filters.forEach(function (f, i) {
    if (i !== filterIdx) used[f.kind] = true;
  });
  var opts = [];
  if (!used.sender) opts.push({value: "sender", label: "Sender"});
  if (!used.message) opts.push({value: "message", label: "Message"});
  if (!used.chat_channel) opts.push({value: "chat_channel", label: "Chat channel"});
  if (!used.uclient_room) opts.push({value: "uclient_room", label: "UClient room"});
  return opts;
}
function scSetFilterKind(filterIdx, newKind) {
  if (!scEditing || !scEditing.trigger) return;
  scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  var cur = scEditing.trigger.filters[filterIdx];
  if (cur && cur.kind === newKind) return;
  scPushHistory();
  scCommitFilterRowInputs(filterIdx);
  if (cur) scRememberFilterDraft(filterIdx);
  scEnsureFilterDrafts();
  var drafts = scEditing._filterDrafts[filterIdx];
  var restored = drafts && drafts[newKind]
    ? JSON.parse(JSON.stringify(drafts[newKind]))
    : scDefaultFilter(newKind);
  scEditing.trigger.filters[filterIdx] = restored;
  scUpdateBlockElement("trigger", 0);
}
function scSetFilterField(filterIdx, field, value) {
  var f = scGetFilterRef(filterIdx);
  if (!f) return;
  scPushHistory();
  if (field === "seconds" || field === "color") f[field] = Number(value) || 0;
  else f[field] = value;
  scRememberFilterDraft(filterIdx);
  scUpdateBlockElement("trigger", 0);
}
function scAddTriggerFilter() {
  if (!scEditing || !scEditing.trigger) return;
  scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  var kinds = ["sender", "message", "chat_channel", "uclient_room"];
  for (var i = 0; i < kinds.length; i++) {
    var k = kinds[i];
    if (!scEditing.trigger.filters.some(function (f) { return f.kind === k; })) {
      scPushHistory();
      scEditing.trigger.filters.push(scDefaultFilter(k));
      scUpdateBlockElement("trigger", 0);
      return;
    }
  }
}
function scRemoveTriggerFilter(filterIdx) {
  if (!scEditing || !scEditing.trigger) return;
  if (filterIdx <= 0) return;
  scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  if (filterIdx >= scEditing.trigger.filters.length) return;
  scPushHistory();
  scCommitFilterRowInputs(filterIdx);
  scEditing.trigger.filters.splice(filterIdx, 1);
  scReindexFilterDrafts(filterIdx);
  scUpdateBlockElement("trigger", 0);
}
function scGetBlockRef(kind, idx) {
  if (!scEditing) return null;
  if (kind === "trigger-filter") return scGetFilterRef(idx);
  return kind === "trigger" ? scEditing.trigger : scEditing.actions[idx];
}
function scSetBlockField(kind, idx, field, value) {
  scPushHistory();
  if (kind === "trigger-filter") {
    if (field === "kind") scSetFilterKind(idx, value);
    else scSetFilterField(idx, field, value);
    return;
  }
  var ref = scGetBlockRef(kind, idx);
  if (!ref) return;
  if (field === "seconds") ref.seconds = scClampWaitSeconds(value);
  else if (field === "color") ref.color = Number(value) || 0;
  else if (field === "enabled") ref[field] = value === "on" || value === "true" || value === true;
  else if (field === "left" && ref.type === "if") {
    ref.left = value;
    scNormalizeIfCondition(ref);
  } else if (field === "op" && ref.type === "if") {
    ref.op = value;
    scNormalizeIfCondition(ref);
  } else ref[field] = value;
  scUpdateBlockElement(kind, idx);
}
function scOptionsForField(field, actionIdx) {
  var ref = actionIdx != null && scEditing && scEditing.actions ? scEditing.actions[actionIdx] : null;
  if (field === "channel") return [{value: "all", label: "All"}, {value: "team", label: "Team"}, {value: "uclient", label: "UClient"}];
  if (field === "match") return [{value: "contains", label: "contains"}, {value: "equals", label: "equals"}, {value: "starts_with", label: "starts with"}];
  if (field === "weapon") return [{value: "hammer", label: "Hammer"}, {value: "gun", label: "Gun"}, {value: "shotgun", label: "Shotgun"}, {value: "grenade", label: "Grenade"}, {value: "laser", label: "Laser"}];
  if (field === "target") return [{value: "player", label: "Player"}, {value: "dummy", label: "Dummy"}];
  if (field === "enabled") return [{value: "on", label: "On"}, {value: "off", label: "Off"}];
  if (field === "property") return [{value: "window_active", label: "Window Active"}];
  if (field === "op" && ref && ref.type === "if") return scIfOpsForVariable(ref.left || "");
  if (field === "right" && ref && ref.type === "if" && scIfLeftValueKind(ref.left || "") === "channel") {
    return [{value: "all", label: "All"}, {value: "team", label: "Team"}, {value: "uclient", label: "UClient"}];
  }
  return [];
}
function scOpenEditor(existing) {
  scEditorAnimGen++;
  scEditing = existing ? JSON.parse(JSON.stringify(existing)) : {
    id: scUuid(), name: "New Shortcut", enabled: true,
    trigger: null, actions: []
  };
  if (scEditing.trigger) scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  if (scEditing.actions) {
    scEditing.actions = scRepairIfBlocks(scEditing.actions.map(function (a, i) { return scNormalizeAction(a, i); }));
  }
  scSeedFilterDrafts();
  scResetHistory();
  scRun = null;
  scUpdatePlayButton();
  $("sc-ed-delete").style.display = existing ? "grid" : "none";
  var ed = $("sc-editor");
  var view = $("shortcuts-view");
  ed.classList.remove("on");
  view.classList.add("editing");
  ed.style.display = "flex";
  ed.setAttribute("aria-hidden", "false");
  scSetDrawerY(scDrawerStage2Ratio(), false);
  scRenderEditor();
  scRenderDrawer();
  requestAnimationFrame(function () {
    requestAnimationFrame(function () {
      ed.classList.add("on");
    });
  });
}
var scEditorAnimGen = 0;
function scFinishEditorClose() {
  var ed = $("sc-editor");
  ed.style.display = "none";
  ed.classList.remove("on");
  ed.setAttribute("aria-hidden", "true");
  $("shortcuts-view").classList.remove("editing");
  scEditing = null;
}
function scCloseEditor() {
  scEndDrag(true);
  scCloseAllPickers(null);
  if (scRunActive()) scStopRun(true);
  var ed = $("sc-editor");
  if (ed.style.display !== "flex") {
    scFinishEditorClose();
    return;
  }
  if (!ed.classList.contains("on")) {
    scFinishEditorClose();
    return;
  }
  var gen = ++scEditorAnimGen;
  ed.classList.remove("on");
  var finished = false;
  function done() {
    if (finished || gen !== scEditorAnimGen) return;
    finished = true;
    ed.removeEventListener("transitionend", onEnd);
    scFinishEditorClose();
  }
  function onEnd(e) {
    if (e.target !== ed) return;
    if (e.propertyName !== "transform" && e.propertyName !== "-webkit-transform") return;
    done();
  }
  ed.addEventListener("transitionend", onEnd);
  setTimeout(done, 480);
}
var SC_DRAWER_FULL = 0;
var SC_DRAWER_PEEK = 0;
var SC_DRAWER_HEIGHT_FRAC = 0.78;
var SC_DRAWER_HEIGHT_MAX = 520;
var SC_DRAWER_STAGE2_VISIBLE = 148;
var SC_DRAWER_CAP_W_MAX = 340;
var SC_DRAWER_CAP_W_MARGIN = 72;
var SC_DRAWER_CAP_H = 148;
var SC_DRAWER_CAP_BOTTOM = 12;
var SC_DRAWER_RUBBER = 0.16;
var SC_DRAWER_FLICK_PX_S = 520;
var SC_DRAWER_FLICK_WINDOW_MS = 110;
var SC_DRAWER_FLICK_PROJECT_S = 0.16;
var scDrawerY = SC_DRAWER_PEEK;
var scDrawerRawY = SC_DRAWER_PEEK;
var scDrawerFullHeight = 520;
function scDrawerFullHeightPx(drawer) {
  var parent = drawer && drawer.parentElement;
  var parentH = parent && parent.clientHeight ? parent.clientHeight : 560;
  return Math.min(Math.round(parentH * SC_DRAWER_HEIGHT_FRAC), SC_DRAWER_HEIGHT_MAX);
}
function scDrawerCapsuleWidth(parentW) {
  return Math.min(SC_DRAWER_CAP_W_MAX, Math.max(260, parentW - SC_DRAWER_CAP_W_MARGIN));
}
function scDrawerMorphT(ratio, stage2) {
  if (ratio <= SC_DRAWER_PEEK) return 0;
  if (ratio >= stage2) return 1;
  return (ratio - SC_DRAWER_PEEK) / (stage2 - SC_DRAWER_PEEK);
}
function scApplyDrawerChrome(drawer, ratio) {
  if (!drawer) return;
  var stage2 = scDrawerStage2Ratio();
  var t = scDrawerMorphT(ratio, stage2);
  var parent = drawer.parentElement;
  var parentW = parent && parent.clientWidth ? parent.clientWidth : window.innerWidth;
  var fullH = scDrawerFullHeightPx(drawer);
  if (t < 0.02) scDrawerFullHeight = fullH;
  else if (drawer.offsetHeight > SC_DRAWER_CAP_H + 48) scDrawerFullHeight = fullH;
  var capW = scDrawerCapsuleWidth(parentW);
  var leftPx = (parentW - capW) * 0.5 * t;
  var wPx = parentW - (parentW - capW) * t;
  var hPx = SC_DRAWER_CAP_H + (fullH - SC_DRAWER_CAP_H) * (1 - t);
  var rTop = 40 - 12 * (1 - t);
  var rBot = 28 * t;
  drawer.style.left = leftPx.toFixed(2) + "px";
  drawer.style.width = wPx.toFixed(2) + "px";
  drawer.style.height = hPx.toFixed(2) + "px";
  drawer.style.bottom = (SC_DRAWER_CAP_BOTTOM * t).toFixed(2) + "px";
  drawer.style.borderRadius = rTop.toFixed(1) + "px " + rTop.toFixed(1) + "px " + rBot.toFixed(1) + "px " + rBot.toFixed(1) + "px";
  drawer.style.boxShadow = t > 0.08
    ? "0 0 0 1px rgba(255,255,255," + (0.07 * t).toFixed(3) + "),0 " + (6 * t).toFixed(1) + "px " + (20 * t).toFixed(1) + "px rgba(255,255,255," + (0.045 * t).toFixed(3) + "),0 " + (14 * t).toFixed(1) + "px " + (40 * t).toFixed(1) + "px rgba(255,255,255," + (0.025 * t).toFixed(3) + ")"
    : "none";
  // Full drawer slides on translateY; the capsule stays pinned to bottom (t=1 -> no slide).
  var slide = ratio * (1 - t);
  drawer.style.transform = "translateY(" + (slide * 100).toFixed(1) + "%)";
  drawer.style.setProperty("--sc-drawer-t", t.toFixed(4));
  drawer.classList.toggle("sc-drawer-capsule", t > 0.88);
}
function scDrawerStage2Ratio() {
  var drawer = $("sc-drawer");
  var h = scDrawerFullHeightPx(drawer) || scDrawerFullHeight || 520;
  return Math.min(0.975, Math.max(0.12, 1 - SC_DRAWER_STAGE2_VISIBLE / h));
}
function scRubberDrawerRatio(raw) {
  var stage2 = scDrawerStage2Ratio();
  if (raw < SC_DRAWER_FULL) {
    var pullUp = SC_DRAWER_FULL - raw;
    var giveUp = SC_DRAWER_RUBBER * (1 - 1 / (1 + pullUp * 4.5));
    return SC_DRAWER_FULL - giveUp;
  }
  if (raw > stage2) {
    var pullDown = raw - stage2;
    var giveDown = SC_DRAWER_RUBBER * (1 - 1 / (1 + pullDown * 4.5));
    return stage2 + giveDown;
  }
  return raw;
}
function scSetDrawerY(rawRatio, animate) {
  var drawer = $("sc-drawer");
  var stage2 = scDrawerStage2Ratio();
  scDrawerRawY = rawRatio;
  var ratio = animate
    ? Math.max(SC_DRAWER_FULL, Math.min(stage2, rawRatio))
    : scRubberDrawerRatio(rawRatio);
  scDrawerY = ratio;
  drawer.classList.toggle("dragging", !animate);
  scApplyDrawerChrome(drawer, ratio);
  if (animate) drawer.classList.remove("dragging");
}
function scDrawerTrackSample(scDrag, clientY) {
  if (!scDrag) return;
  if (!scDrag.samples) scDrag.samples = [];
  var now = performance.now();
  scDrag.samples.push({y: clientY, t: now});
  var cut = now - SC_DRAWER_FLICK_WINDOW_MS;
  while (scDrag.samples.length > 1 && scDrag.samples[0].t < cut) scDrag.samples.shift();
}
function scDrawerDragVelocityPxS(scDrag) {
  var samples = scDrag && scDrag.samples;
  if (!samples || samples.length < 2) return 0;
  var first = samples[0];
  var last = samples[samples.length - 1];
  var dt = (last.t - first.t) / 1000;
  if (dt < 0.02) return 0;
  return (last.y - first.y) / dt;
}
function scSnapDrawerY() {
  var raw = scDrawerRawY;
  var stage2 = scDrawerStage2Ratio();
  var drawer = $("sc-drawer");
  var h = scDrawerFullHeightPx(drawer) || scDrawerFullHeight || 520;
  var vel = scDrag && scDrag.type === "drawer" ? scDrawerDragVelocityPxS(scDrag) : 0;
  if (Math.abs(vel) >= SC_DRAWER_FLICK_PX_S) {
    if (vel < 0) scSetDrawerY(SC_DRAWER_FULL, true);
    else scSetDrawerY(stage2, true);
    return;
  }
  var projected = raw + (vel / h) * SC_DRAWER_FLICK_PROJECT_S;
  var mid = (SC_DRAWER_FULL + stage2) * 0.5;
  if (projected < mid) scSetDrawerY(SC_DRAWER_FULL, true);
  else scSetDrawerY(stage2, true);
}
function scSnapDrawerToStage2() {
  requestAnimationFrame(function () {
    scSetDrawerY(scDrawerStage2Ratio(), true);
  });
}
function scDrawerAtStage2() {
  return scDrawerMorphT(scDrawerY, scDrawerStage2Ratio()) >= 0.99;
}
function scExpandDrawerToPeekFromSearch() {
  if (!scDrawerAtStage2()) return;
  scSetDrawerY(SC_DRAWER_FULL, true);
}
function scUpdateDrawerDrag(clientY) {
  if (!scDrag || scDrag.type !== "drawer") return;
  var drawer = $("sc-drawer");
  var h = scDrawerFullHeightPx(drawer) || scDrawerFullHeight || 1;
  var delta = (clientY - scDrag.startY) / h;
  if (Math.abs(clientY - scDrag.startY) > 3) scDrag.moved = true;
  scDrawerTrackSample(scDrag, clientY);
  scSetDrawerY(scDrag.startRatio + delta, false);
}
function scAddCatalogBlock(kind, id) {
  if (!scEditing) return;
  scPushHistory();
  var added = false;
  if (kind === "trigger") {
    var trig = SC_TRIGGERS.find(function (t) { return t.id === id; });
    if (!trig) return;
    scEditing.trigger = scNormalizeTrigger(JSON.parse(JSON.stringify(trig.defaults)));
    scSeedFilterDrafts();
    var existing = $("sc-ed-canvas").querySelector('.sc-block[data-block-kind="trigger"]');
    if (existing) scUpdateBlockElement("trigger", 0);
    else scAppendBlockElement("trigger", 0);
    added = true;
  } else {
    var act = SC_ACTIONS.find(function (a) { return a.id === id; });
    if (!act) return;
    if (!scEditing.actions) scEditing.actions = [];
    var copy = JSON.parse(JSON.stringify(act.defaults));
    if (copy.type === "text") copy.as = scNextTextVarName();
    if (copy.type === "if") {
      var startIdx = scEditing.actions.length;
      scEditing.actions.push(copy);
      scEditing.actions.push({type: "otherwise"});
      scEditing.actions.push({type: "end_if"});
      scRenderEditor({enterActionIdx: startIdx});
    } else {
      scEditing.actions.push(copy);
      scAppendBlockElement("action", scEditing.actions.length - 1);
    }
    added = true;
  }
  if (added) scSnapDrawerToStage2();
}
function scMoveIfGroup(fromIfIdx, toIdx) {
  if (!scEditing || !scEditing.actions) return;
  var endIdx = scFindMatchingEndIf(scEditing.actions, fromIfIdx);
  if (endIdx < 0) return;
  if (toIdx === fromIfIdx || toIdx === endIdx + 1) return;
  scPushHistory();
  var len = endIdx - fromIfIdx + 1;
  var group = scEditing.actions.splice(fromIfIdx, len);
  if (toIdx > fromIfIdx) toIdx -= len;
  toIdx = Math.max(0, Math.min(toIdx, scEditing.actions.length));
  Array.prototype.splice.apply(scEditing.actions, [toIdx, 0].concat(group));
  scRenderEditor();
}
function scMoveAction(fromIdx, toIdx) {
  if (!scEditing || !scEditing.actions) return;
  if (fromIdx < 0 || toIdx < 0) return;
  if (fromIdx >= scEditing.actions.length) return;
  var act = scEditing.actions[fromIdx];
  if (act.type === "otherwise" || act.type === "end_if") return;
  if (act.type === "if") {
    scMoveIfGroup(fromIdx, toIdx);
    return;
  }
  // toIdx is an insert position in the list before the block is taken out.
  if (toIdx === fromIdx || toIdx === fromIdx + 1) return;
  scPushHistory();
  var item = scEditing.actions.splice(fromIdx, 1)[0];
  if (toIdx > fromIdx) toIdx--;
  toIdx = Math.max(0, Math.min(toIdx, scEditing.actions.length));
  scEditing.actions.splice(toIdx, 0, item);
  scRenderEditor();
}
function scCanvasInsertIndex(clientY, dragCtx) {
  var canvas = $("sc-ed-canvas");
  var blocks = canvas.querySelectorAll(".sc-block[data-block-kind='action']");
  if (!blocks.length) return 0;
  var insertIdx = Number(blocks[blocks.length - 1].dataset.blockIdx) + 1;
  for (var i = 0; i < blocks.length; i++) {
    var r = blocks[i].getBoundingClientRect();
    if (clientY < r.top + r.height * 0.5) {
      insertIdx = Number(blocks[i].dataset.blockIdx);
      break;
    }
  }
  if (dragCtx && dragCtx.groupEnd != null && dragCtx.groupEnd >= 0) {
    var from = dragCtx.idx;
    var to = dragCtx.groupEnd;
    if (insertIdx > from && insertIdx <= to) {
      insertIdx = insertIdx <= Math.floor((from + to) / 2) ? from : to + 1;
    }
  }
  return insertIdx;
}
function scBindCanvasDropGap(insertIdx) {
  var canvas = $("sc-ed-canvas");
  var old = canvas.querySelector(".sc-drop-gap");
  if (old) old.remove();
  if (insertIdx == null) {
    canvas.classList.remove("drag-over");
    return;
  }
  canvas.classList.add("drag-over");
  var gap = document.createElement("div");
  gap.className = "sc-drop-gap";
  var blocks = canvas.querySelectorAll(".sc-block[data-block-kind='action']");
  var target = null;
  for (var i = 0; i < blocks.length; i++) {
    if (Number(blocks[i].dataset.blockIdx) >= insertIdx) {
      target = blocks[i];
      break;
    }
  }
  if (!target) canvas.appendChild(gap);
  else canvas.insertBefore(gap, target);
}
function scEnsureDragGhost(text) {
  if (!scDragGhost) {
    scDragGhost = document.createElement("div");
    scDragGhost.className = "sc-drag-ghost";
    document.body.appendChild(scDragGhost);
  }
  scDragGhost.textContent = text || "";
  scDragGhost.style.display = text ? "block" : "none";
}
function scEndDrag(cancel) {
  if (!scDrag) return;
  if (scDrag.type === "drawer") {
    if (!cancel) scSnapDrawerY();
    $("sc-drawer").classList.remove("dragging");
    scDrag = null;
    return;
  }
  if (scDrag.type === "block" && !cancel && scDrag.overIdx != null) {
    var moved = scDrag.overIdx !== scDrag.idx;
    if (scDrag.groupEnd != null && scDrag.groupEnd >= 0) {
      moved = moved && !(scDrag.overIdx > scDrag.idx && scDrag.overIdx <= scDrag.groupEnd + 1);
      if (moved) scMoveIfGroup(scDrag.idx, scDrag.overIdx);
    } else if (moved) {
      scMoveAction(scDrag.idx, scDrag.overIdx);
    }
  }
  if (scDrag.el) scDrag.el.classList.remove("dragging");
  scBindCanvasDropGap(null);
  scEnsureDragGhost("");
  scDrag = null;
}
var scDrag = null;
var scDragGhost = null;
document.addEventListener("pointermove", function (e) {
  if (!scDrag) return;
  if (scDrag.type === "drawer") {
    scUpdateDrawerDrag(e.clientY);
    return;
  }
  scEnsureDragGhost(scDrag.label || "");
  scDragGhost.style.left = e.clientX + "px";
  scDragGhost.style.top = e.clientY + "px";
  if (scDrag.type === "block") {
    scDrag.overIdx = scCanvasInsertIndex(e.clientY, scDrag);
    scBindCanvasDropGap(scDrag.overIdx);
  }
});
document.addEventListener("pointerup", function () { scEndDrag(false); });
document.addEventListener("pointercancel", function () { scEndDrag(true); });
function scRenderDrawer() {
  var chips = [{id: "all", label: "All"}, {id: "triggers", label: "When"}, {id: "actions", label: "Do"}];
  $("sc-drawer-chips").innerHTML = chips.map(function (c) {
    return '<button type="button" class="sc-chip' + (scDrawerFilter === c.id ? " on" : "") + '" data-chip="' + c.id + '">' + c.label + '</button>';
  }).join("");
  var q = ($("sc-drawer-search").value || "").toLowerCase();
  var html = "";
  if (scDrawerFilter === "all" || scDrawerFilter === "triggers") {
    SC_TRIGGER_SECTIONS.forEach(function (sec) {
      var rows = SC_TRIGGERS.filter(function (t) {
        return t.category === sec.id && scCatalogMatches(t, q, true);
      });
      if (!rows.length) return;
      html += '<div class="sc-drawer-section">' + esc(sec.label) + '</div>';
      rows.forEach(function (t) { html += scCatalogRow("trigger", t); });
    });
  }
  if (scDrawerFilter === "all" || scDrawerFilter === "actions") {
    SC_ACTION_SECTIONS.forEach(function (sec) {
      var rows = SC_ACTIONS.filter(function (a) {
        if (a.id === "otherwise" || a.id === "end_if") return false;
        return (a.category || "actions") === sec.id && scCatalogMatches(a, q, false);
      });
      if (!rows.length) return;
      html += '<div class="sc-drawer-section">' + esc(sec.label) + '</div>';
      rows.forEach(function (a) { html += scCatalogRow("action", a); });
    });
  }
  $("sc-drawer-list").innerHTML = html || '<div class="sc-ed-empty" style="margin-top:24px"><b>No matches</b>Try another search or filter.</div>';
}
$("sc-fab").addEventListener("click", function () { scOpenEditor(null); });
$("sc-ed-back").addEventListener("click", function () { scCloseEditor(); });
$("sc-ed-save").addEventListener("click", function () {
  if (!scEditing) return;
  if (!scEditing.trigger) { scToast("Add a When trigger"); return; }
  if (!scEditing.actions || !scEditing.actions.length) { scToast("Add at least one action"); return; }
  scCommitAllPillInputs($("sc-ed-canvas"));
  scEditing.trigger = scCleanTriggerForSave(scEditing.trigger);
  scEditing.actions = (scEditing.actions || []).map(scCleanActionForSave);
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
$("sc-drawer-grab").addEventListener("pointerdown", function (e) {
  if (e.button !== 0) return;
  scDrag = {type: "drawer", startY: e.clientY, startRatio: scDrawerRawY, moved: false, samples: []};
  scDrawerTrackSample(scDrag, e.clientY);
  $("sc-drawer").classList.add("dragging");
  e.currentTarget.setPointerCapture(e.pointerId);
  e.preventDefault();
});
$("sc-drawer-grab").addEventListener("pointermove", function (e) {
  scUpdateDrawerDrag(e.clientY);
});
$("sc-ed-title").addEventListener("focusin", function () { scTitleDirty = false; });
$("sc-ed-title").addEventListener("input", function (e) {
  if (!scEditing) return;
  if (!scTitleDirty) {
    scTitleDirty = true;
    scPushHistory();
  }
  scEditing.name = e.target.value;
});
$("sc-tb-undo").addEventListener("click", scUndo);
$("sc-tb-redo").addEventListener("click", scRedo);
$("sc-tb-play").addEventListener("click", function () {
  if (!scEditing) return;
  if (scRunActive()) {
    scStopRun(true);
    return;
  }
  if (!lastState || !lastState.gameRunning) { scToast("Start the game first"); return; }
  scCommitAllPillInputs($("sc-ed-canvas"));
  var actions = (scEditing.actions || []).map(scCleanActionForSave);
  if (!actions.length) { scToast("Add at least one action"); return; }
  scRun = {runId: scUuid(), status: "running", step: -1, total: actions.length, waitRemaining: null, results: []};
  scApplyRunState();
  scUpdatePlayButton();
  send({cmd: "automationRun", runId: scRun.runId, actions: actions});
});
$("sc-drawer-search").addEventListener("input", function (e) {
  if ((e.target.value || "").length > 0) scExpandDrawerToPeekFromSearch();
  scRenderDrawer();
});
$("sc-drawer-chips").addEventListener("click", function (e) {
  var chip = e.target.closest("[data-chip]");
  if (!chip) return;
  scDrawerFilter = chip.dataset.chip;
  scRenderDrawer();
});
$("sc-drawer-list").addEventListener("click", function (e) {
  var row = e.target.closest(".sc-catalog");
  if (!row || !scEditing) return;
  scAddCatalogBlock(row.dataset.addKind, row.dataset.addId);
});
$("sc-ed-canvas").addEventListener("pointerdown", function (e) {
  var grip = e.target.closest(".sc-block-grip[data-drag-kind='action']");
  if (!grip || !scEditing || e.button !== 0) return;
  var idx = Number(grip.dataset.dragIdx);
  var block = grip.closest(".sc-block");
  var act = scEditing.actions[idx];
  var groupEnd = act && act.type === "if" ? scFindMatchingEndIf(scEditing.actions, idx) : -1;
  scDrag = {type: "block", idx: idx, groupEnd: groupEnd, label: block ? block.textContent.trim().slice(0, 48) : "Action", el: block, overIdx: idx};
  if (block) block.classList.add("dragging");
  scEnsureDragGhost(scDrag.label);
  grip.setPointerCapture(e.pointerId);
  e.preventDefault();
});
$("sc-ed-canvas").addEventListener("focusin", function (e) {
  var input = e.target.closest("input.sc-pill.input");
  var textarea = e.target.closest("textarea.sc-text-body-input");
  if (textarea) {
    textarea.dataset.scOrig = textarea.value;
  } else if (input) {
    input.dataset.scOrig = input.value;
    scFitPillInput(input);
    requestAnimationFrame(function () {
      var len = input.value.length;
      try { input.setSelectionRange(len, len); } catch (err) {}
    });
  }
  var smartField = e.target.closest(".sc-smart-field");
  if (smartField && e.target.closest(".sc-smart-trigger")) {
    var trig = e.target.closest(".sc-smart-trigger");
    if (trig && (trig.tagName === "INPUT" || trig.tagName === "TEXTAREA")) {
      if (String(trig.value || "").length === 0) scSmartFieldShowMenu(smartField, true);
      else scCloseAllPickers(null);
      return;
    }
    scSmartFieldShowMenu(smartField);
  }
});
$("sc-ed-canvas").addEventListener("input", function (e) {
  var textarea = e.target.closest("textarea.sc-text-body-input");
  var input = e.target.closest("input.sc-pill.input");
  if (!input && !textarea) return;
  if (textarea) input = textarea;
  if (!input.dataset.scDirty) {
    input.dataset.scDirty = "1";
    scPushHistory();
  }
  if (input.dataset.input === "seconds") {
    var raw = String(input.value || "");
    var digitsOnly = raw.replace(/[^0-9]/g, "").replace(/^0+(?=\d)/, "");
    if (digitsOnly !== raw) {
      var caret = Math.max(0, (input.selectionStart || 0) - (raw.length - digitsOnly.length));
      input.value = digitsOnly;
      try { input.setSelectionRange(caret, caret); } catch (err) {}
    }
  }
  if (input.dataset.input === "messageText") {
    var ref = scGetBlockRef(input.dataset.kind, Number(input.dataset.idx || 0));
    if (ref) {
      scNormalizeAction(ref, Number(input.dataset.idx || 0));
      ref.messageMode = "text";
      ref.messageText = input.value;
    }
  }
  if (input.dataset.input === "textBody") {
    var tRef = scGetBlockRef(input.dataset.kind, Number(input.dataset.idx || 0));
    if (tRef) {
      scNormalizeAction(tRef, Number(input.dataset.idx || 0));
      tRef.parts = [{mode: "text", text: input.value}];
    }
  }
  if (input.classList.contains("sc-smart-trigger")) {
    var field = input.closest(".sc-smart-field");
    if (String(input.value || "").length === 0) scSmartFieldShowMenu(field, true);
    else scSmartFieldHideMenu(field);
  }
  scFitPillInput(input);
});
$("sc-ed-canvas").addEventListener("focusout", function (e) {
  var input = e.target.closest("input.sc-pill.input");
  var textarea = e.target.closest("textarea.sc-text-body-input");
  if (textarea) {
    scCommitPillInput(textarea, false);
  } else if (input) {
    scCommitPillInput(input, false);
    scFitPillInput(input);
  }
  var smartField = e.target.closest(".sc-smart-field");
  if (!smartField) return;
  clearTimeout(scSmartBlurTimer);
  scSmartBlurTimer = setTimeout(function () {
    var active = document.activeElement;
    if (smartField.contains(active)) return;
    scSmartFieldHideMenu(smartField);
  }, 130);
});
$("sc-ed-canvas").addEventListener("keydown", function (e) {
  var input = e.target.closest("input.sc-pill.input");
  if (!input) return;
  if (e.key === "Enter" && input.tagName !== "TEXTAREA") { e.preventDefault(); input.blur(); }
  if (e.key === "Escape") {
    e.preventDefault();
    scCommitPillInput(input, true);
    input.blur();
  }
  if (e.key === "Backspace" && scHandleMultiValueBackspace(input)) {
    e.preventDefault();
  }
});
$("sc-ed-canvas").addEventListener("mousedown", function (e) {
  if (e.target.closest(".sc-smart-menu-item")) e.preventDefault();
  var textTrig = e.target.closest("input.sc-smart-trigger, textarea.sc-smart-trigger");
  if (textTrig) {
    var sf = textTrig.closest(".sc-smart-field");
    if (sf && String(textTrig.value || "").length === 0) scSmartFieldShowMenu(sf, true);
    else scCloseAllPickers(null);
    return;
  }
  if (e.target.closest("input.sc-pill.input")) e.stopPropagation();
});
$("sc-ed-canvas").addEventListener("click", function (e) {
  if (!scEditing) return;
  var addFilter = e.target.closest("[data-add-filter]");
  if (addFilter) {
    scAddTriggerFilter();
    return;
  }
  var addSenderName = e.target.closest("[data-add-sender-name]");
  if (addSenderName) {
    scAddSenderNameSlot(Number(addSenderName.dataset.addSenderName));
    return;
  }
  var addServerTarget = e.target.closest("[data-add-server-target]");
  if (addServerTarget) {
    scAddServerTargetSlot();
    return;
  }
  var remFilter = e.target.closest("[data-remove-filter]");
  if (remFilter) {
    scRemoveTriggerFilter(Number(remFilter.dataset.removeFilter));
    return;
  }
  var del = e.target.closest("[data-del-action]");
  if (del) {
    scRemoveActionBlock(Number(del.dataset.delAction));
    return;
  }
  var menuItem = e.target.closest(".sc-smart-menu-item");
  if (menuItem) {
    var pickMenu = menuItem.closest(".sc-smart-menu");
    var field = (pickMenu && pickMenu._scPortalField) || menuItem.closest(".sc-smart-field");
    scApplySmartPick(field, menuItem.dataset.pickType, menuItem.dataset.pickId);
    return;
  }
  var smartTrigger = e.target.closest(".sc-smart-trigger");
  if (smartTrigger) {
    var smartField = smartTrigger.closest(".sc-smart-field");
    if (!smartField) return;
    if (smartTrigger.tagName === "BUTTON") {
      scSmartFieldShowMenu(smartField, true);
      return;
    }
    if ((smartTrigger.tagName === "INPUT" || smartTrigger.tagName === "TEXTAREA") &&
        String(smartTrigger.value || "").length === 0) {
      scSmartFieldShowMenu(smartField, true);
      return;
    }
  }
  if (!e.target.closest(".sc-smart-field") && !e.target.closest(".sc-pop") &&
      !e.target.closest(".sc-pill[data-field]") && !e.target.closest(".sc-smart-menu-item")) {
    scCloseAllPickers(null);
  }
  var pill = e.target.closest(".sc-pill");
  if (!pill || pill.tagName === "INPUT" || pill.classList.contains("sc-smart-trigger")) return;
  var kind = pill.dataset.kind;
  var idx = Number(pill.dataset.idx || 0);
  var field = pill.dataset.field;
  var cur = scGetBlockRef(kind, idx);
  var curVal = cur ? cur[field] : null;
  if (field === "enabled") curVal = cur && cur.enabled ? "on" : "off";
  if (field === "op" && cur && cur.type === "if") curVal = cur.op;
  var opts = field === "kind" ? scFilterFieldOptions(idx) : scOptionsForField(field, idx);
  if (!opts.length) return;
  scOpenPop(pill, opts, function (val) { scSetBlockField(kind, idx, field, val); }, curVal);
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
    var on = sc.enabled !== false;
    toggle.classList.toggle("on", on);
    var row = toggle.closest(".sc-row");
    if (row) row.classList.toggle("off", !on);
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
  // Nothing will report progress once the game is gone, so drop the run.
  if (scRunActive() && !st.gameRunning) scStopRun(true);
  scUpdatePlayButton();
};

window.addEventListener("resize", moveIndicator);
requestAnimationFrame(function () { moveIndicator(); send({cmd: "ready"}); });
</script>
</body>
</html>
)HTMLDOC";
