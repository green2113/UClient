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
//               shortcuts[] {id, name, kind, enabled, trigger?, actions[]},
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
@font-face{
  font-family:'Nunito';
  font-weight:400 800;
  font-style:normal;
  font-display:swap;
  src:url('https://cdn.jsdelivr.net/npm/@fontsource/nunito@5.2.5/files/nunito-latin-400-normal.woff2') format('woff2');
  unicode-range:U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+0304,U+0308,U+0329,U+2000-206F,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,U+FEFF,U+FFFD;
}
@font-face{
  font-family:'Nunito';
  font-weight:600;
  font-style:normal;
  font-display:swap;
  src:url('https://cdn.jsdelivr.net/npm/@fontsource/nunito@5.2.5/files/nunito-latin-600-normal.woff2') format('woff2');
  unicode-range:U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+0304,U+0308,U+0329,U+2000-206F,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,U+FEFF,U+FFFD;
}
@font-face{
  font-family:'Nunito';
  font-weight:700;
  font-style:normal;
  font-display:swap;
  src:url('https://cdn.jsdelivr.net/npm/@fontsource/nunito@5.2.5/files/nunito-latin-700-normal.woff2') format('woff2');
  unicode-range:U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+0304,U+0308,U+0329,U+2000-206F,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,U+FEFF,U+FFFD;
}
@font-face{
  font-family:'Nunito';
  font-weight:800;
  font-style:normal;
  font-display:swap;
  src:url('https://cdn.jsdelivr.net/npm/@fontsource/nunito@5.2.5/files/nunito-latin-800-normal.woff2') format('woff2');
  unicode-range:U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+0304,U+0308,U+0329,U+2000-206F,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,U+FEFF,U+FFFD;
}
@font-face{
  font-family:'Pretendard Variable';
  font-weight:45 920;
  font-style:normal;
  font-display:swap;
  src:url('https://cdn.jsdelivr.net/gh/orioncactus/pretendard@v1.3.9/packages/pretendard/dist/public/static/PretendardVariable.woff2') format('woff2');
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
  --sc-r-control:16px;
  --sc-r-pill:14px;
  --sc-r-tile:clamp(22px,22%,30px);
  --sc-r-tile-ico:12px;
  --sc-pill-fill:rgba(124,108,240,.32);
  --sc-pill-fill-nested:rgba(124,108,240,.52);
  --sc-pill-text:#ddd8ff;
  --sc-pill-text-nested:#f2efff;
  --sc-surface:#181a22;
  --sc-surface-raised:#22242e;
  --sc-surface-inset:#14161c;
  --play-fg:#ffffff;
  --font-apple-round:"SF Pro Rounded","SF Pro Display",ui-rounded,"Nunito","Pretendard Variable","Pretendard",-apple-system,BlinkMacSystemFont,"Segoe UI Variable","Segoe UI",system-ui,sans-serif;
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
#shortcuts-view{display:none;grid-column:2/4;min-width:0;padding:56px 34px 34px 44px;flex-direction:column;background:linear-gradient(160deg,#12141c 0%,#08090d 100%);position:relative;overflow:hidden;font-family:var(--font-apple-round);letter-spacing:-.01em}
#shell.shortcuts-mode #shortcuts-view{display:flex}
#sc-editor{font-family:var(--font-apple-round);letter-spacing:-.01em}
.sc-toast{font-family:var(--font-apple-round)}
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
.sc-lib-tabs{position:relative;display:flex;align-items:center;gap:2px;padding:3px;border-radius:999px;background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08);box-shadow:inset 0 1px 0 rgba(255,255,255,.04);flex:0 1 auto;min-width:0}
.sc-lib-tabs-indicator{position:absolute;top:3px;left:0;height:calc(100% - 6px);border-radius:999px;background:rgba(124,108,240,.32);box-shadow:0 2px 10px rgba(124,108,240,.22);transition:transform .36s var(--spring),width .36s var(--spring);pointer-events:none;z-index:0}
.sc-lib-tab{position:relative;z-index:1;border:0;border-radius:999px;padding:10px 18px;font:inherit;font-size:14px;font-weight:600;color:var(--dim);background:transparent;cursor:pointer;white-space:nowrap;transition:color .22s var(--ease)}
.sc-lib-tab.on{color:#fff}
.sc-lib-panels{flex:1;min-height:0;display:grid;grid-template-columns:1fr;grid-template-rows:1fr;overflow:hidden}
.sc-bottom-dock{position:absolute;left:50%;bottom:20px;transform:translateX(-50%);display:none;z-index:26;pointer-events:none}
.sc-bottom-dock-inner{display:flex;align-items:center;gap:10px;pointer-events:auto}
#shell.shortcuts-mode .sc-bottom-dock{display:block}
#shortcuts-view[data-sc-lib="shortcuts"] #sc-fab-auto,#shortcuts-view[data-sc-lib="automation"] #sc-fab-manual{display:none}
.sc-lib-panel{grid-area:1/1;display:flex;flex-direction:column;min-height:0;padding-bottom:88px;opacity:0;transform:translateX(16px);pointer-events:none;transition:opacity .38s var(--spring),transform .38s var(--spring);will-change:opacity,transform}
.sc-lib-panel.on{opacity:1;transform:translateX(0);pointer-events:auto;z-index:1}
#sc-panel-shortcuts:not(.on){transform:translateX(-16px)}
#sc-panel-automation:not(.on){transform:translateX(16px)}
.sc-page-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px}
.sc-page-head h1{font:800 28px/1.05 inherit;letter-spacing:-.03em;margin:0}
.sc-panel-body{flex:1;min-height:0;display:flex;flex-direction:column;overflow:hidden}
.sc-shortcuts-card .sc-gallery-search-wrap{margin:0 0 12px}
.sc-shortcuts-card .sc-gallery-grid{flex:1;min-height:0;padding:0}
.sc-list-body{flex:1;min-height:0;overflow-y:auto}
.sc-list-body .sc-empty{padding:48px 16px}
.sc-gallery-search-wrap{display:flex;align-items:center;gap:10px;flex:0 0 auto;padding:0 14px;border-radius:14px;background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.06);margin-bottom:16px}
.sc-gallery-search-icon{display:flex;align-items:center;justify-content:center;flex:0 0 auto;width:20px;height:20px;color:var(--muted);opacity:.85}
.sc-gallery-search-icon svg{width:18px;height:18px;display:block}
.sc-gallery-search{flex:1;min-width:0;border:0;border-radius:0;padding:12px 0;font:inherit;font-size:15px;color:var(--text);background:transparent;margin:0;outline:none}
.sc-gallery-search::-webkit-search-cancel-button{-webkit-appearance:none}
.sc-gallery-grid{overflow-y:auto;display:grid;grid-template-columns:repeat(auto-fill,minmax(156px,1fr));gap:11px;align-content:start;max-width:780px}
.sc-gallery-grid::-webkit-scrollbar{width:8px}
.sc-gallery-grid::-webkit-scrollbar-thumb{border-radius:999px;background:rgba(255,255,255,.14)}
.sc-tile{position:relative;display:flex;flex-direction:column;border:0;border-radius:var(--sc-r-tile);min-height:104px;padding:13px 14px 12px;text-align:left;color:#fff;cursor:pointer;overflow:hidden;box-shadow:0 8px 24px rgba(0,0,0,.28);transition:filter .18s var(--ease),box-shadow .18s var(--ease)}
.sc-tile::after{content:"";position:absolute;inset:0;border-radius:inherit;background:rgba(255,255,255,0);pointer-events:none;transition:background .18s var(--ease)}
.sc-tile:hover{filter:brightness(1.07);box-shadow:0 10px 26px rgba(0,0,0,.32)}
.sc-tile:hover::after{background:rgba(255,255,255,.07)}
.sc-tile:active{filter:brightness(.93)}
.sc-tile:active::after{background:rgba(0,0,0,.06)}
.sc-tile-top{display:flex;align-items:flex-start;justify-content:space-between;gap:6px;flex:0 0 auto}
.sc-tile-ico{width:34px;height:34px;border-radius:var(--sc-r-tile-ico);display:grid;place-items:center;font-size:15px;background:rgba(0,0,0,.18)}
.sc-tile-play{width:28px;height:28px;border:0;border-radius:50%;background:rgba(255,255,255,.22);color:#fff;display:grid;place-items:center;cursor:pointer;flex:0 0 auto;padding:0}
.sc-tile-play:hover{background:rgba(255,255,255,.32)}
.sc-tile-play svg{width:15px;height:15px;display:block}
.sc-tile-name{font:700 14px/1.25 inherit;letter-spacing:-.02em;margin-top:auto;padding-top:10px;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden}
.sc-tile.tone-brown{background:linear-gradient(145deg,#8b6914,#6b4f0f)}
.sc-tile.tone-slate{background:linear-gradient(145deg,#4a5568,#2d3748)}
.sc-tile.tone-green{background:linear-gradient(145deg,#2d6a4f,#1b4332)}
.sc-tile.tone-blue{background:linear-gradient(145deg,#1d4e89,#1a365d)}
.sc-tile.tone-purple{background:linear-gradient(145deg,#5b21b6,#4c1d95)}
.sc-tile.tone-rose{background:linear-gradient(145deg,#9f1239,#881337)}
.sc-fab{width:44px;height:44px;border:0;border-radius:50%;background:var(--accent);color:#fff;font-size:22px;line-height:1;cursor:pointer;box-shadow:0 6px 20px rgba(124,108,240,.38),inset 0 1px 0 rgba(255,255,255,.16);flex:0 0 auto;display:grid;place-items:center;transition:transform .12s ease-out,background .18s var(--ease)}
.sc-fab:hover{transform:scale(1.05);background:var(--accent-hi)}
.sc-fab:active{transform:scale(.94);transition:transform 80ms ease-out}
#shortcuts-view.editing .sc-bottom-dock{opacity:0;pointer-events:none;transition:opacity .2s var(--ease)}
#sc-editor{position:absolute;top:56px;right:16px;bottom:16px;left:16px;z-index:25;display:none;flex-direction:column;border-radius:var(--sc-r-sheet);border:1px solid rgba(255,255,255,.10);background:var(--sc-surface);color:var(--text);box-shadow:0 12px 40px rgba(0,0,0,.38);overflow:clip;transform:translateY(calc(100% + 24px));transition:transform .4s var(--spring);pointer-events:none;will-change:transform}
#sc-editor.on{display:flex;transform:translateY(0);pointer-events:auto}
#shortcuts-view.editing .sc-panel-body{pointer-events:none}
@media (prefers-reduced-motion:reduce){
  #sc-editor{transition:opacity .22s ease;transform:none!important;opacity:0}
  #sc-editor.on{opacity:1}
  .sc-lib-panel,.sc-lib-tabs-indicator{transition-duration:.01ms!important}
  .sc-lib-panel{transform:none!important}
}
.sc-ed-head{display:grid;grid-template-columns:1fr auto 1fr;align-items:center;gap:8px;padding:10px 12px 10px 10px;background:var(--sc-surface);border-bottom:1px solid rgba(255,255,255,.06);flex:0 0 auto}
.sc-ed-back{justify-self:start;grid-column:1}
.sc-ed-head-center{grid-column:2;justify-self:center;min-width:0;max-width:min(68vw,440px)}
.sc-ed-trailing{justify-self:end;grid-column:3}
.sc-ed-title-trigger{display:inline-flex;align-items:center;gap:6px;max-width:100%;border:0;border-radius:999px;background:transparent;color:var(--text);font:inherit;cursor:pointer;padding:6px 10px 6px 6px;transition:background .14s var(--ease),color .14s var(--ease)}
.sc-ed-title-trigger:hover{background:rgba(255,255,255,.07)}
.sc-ed-title-trigger:active{background:rgba(255,255,255,.1)}
.sc-ed-title-trigger[aria-expanded="true"]{background:rgba(255,255,255,.08)}
.sc-ed-title-tile{width:28px;height:28px;border-radius:8px;display:grid;place-items:center;flex:0 0 auto;background:rgba(124,108,240,.22);color:var(--accent-hi);overflow:hidden}
.sc-ed-title-tile .sc-block-icon-svg{width:16px;height:16px}
.sc-ed-title-text{font:700 17px/1.2 inherit;letter-spacing:-.02em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-width:0;max-width:min(46vw,280px)}
.sc-ed-title-chev{display:grid;place-items:center;flex:0 0 auto;width:22px;height:22px;border-radius:50%;color:var(--dim);opacity:.92}
.sc-ed-title-chev .sc-ed-chev-svg{width:18px;height:18px;display:block}
.sc-ed-head-center.is-renaming .sc-ed-title-trigger{display:none}
.sc-ed-head-center.is-renaming .sc-ed-title-input{display:block}
.sc-ed-title-input{display:none;width:100%;min-width:180px;max-width:min(68vw,440px);margin:0 auto;text-align:center}
.sc-ed-back,.sc-ed-icon{width:40px;height:40px;min-width:40px;min-height:40px;aspect-ratio:1;border:0;border-radius:50%;padding:0;display:grid;place-items:center;cursor:pointer;flex:0 0 auto;transition:transform 80ms ease-out,background .15s var(--ease),color .15s var(--ease),opacity .15s var(--ease)}
.sc-ed-back{background:transparent;color:var(--dim)}
.sc-ed-back:hover{background:rgba(255,255,255,.07);color:#fff}
.sc-ed-back:active,.sc-ed-icon:active{transform:scale(.92)}
.sc-ed-back svg,.sc-ed-icon svg{width:20px;height:20px;display:block;flex:0 0 auto;overflow:visible}
.sc-ed-delete svg{width:18px;height:18px}
.sc-ed-title-input{border:0;background:transparent;font:700 17px/1.2 inherit;letter-spacing:-.02em;color:var(--text);outline:none;min-width:0}
.sc-ed-title-input::placeholder{color:var(--muted)}
.sc-ed-trailing{display:flex;align-items:center;gap:8px;flex:0 0 auto}
.sc-pop.sc-ed-title-pop .sc-smart-menu-inner{min-width:220px;padding:6px}
.sc-ed-title-menu-item{display:flex!important;align-items:center;gap:8px;width:100%;font:600 13px/1.35 inherit;padding:8px 10px;border-radius:10px;background:transparent;border:0;color:var(--text)}
.sc-ed-title-menu-item .sc-ed-menu-ico{width:16px;height:16px;display:grid;place-items:center;flex:0 0 auto;color:var(--dim)}
.sc-ed-title-menu-item .sc-ed-menu-ico svg{width:14px;height:14px;display:block;stroke-width:1.75}
.sc-ed-title-menu-item .sc-ed-menu-label{flex:1;min-width:0;text-align:left;font:inherit}
.sc-ed-title-menu-item.is-destructive,.sc-ed-title-menu-item.is-destructive .sc-ed-menu-ico{color:#ff6b63}
.sc-ed-title-menu-item.is-destructive:hover,.sc-ed-title-menu-item.is-destructive:focus-visible{background:rgba(255,69,58,.12);color:#ff8a84;outline:none}
.sc-ed-title-menu-item.is-destructive:hover .sc-ed-menu-ico,.sc-ed-title-menu-item.is-destructive:focus-visible .sc-ed-menu-ico{color:#ff8a84}
.sc-ed-save{background:rgba(124,108,240,.24);color:var(--accent-hi);box-shadow:none}
.sc-ed-save:hover{background:rgba(124,108,240,.36);box-shadow:none}
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
.sc-if-group{position:relative;margin-bottom:10px;padding-left:0}
.sc-if-group.sc-enter{animation:scIfGroupIn .36s var(--spring) both}
.sc-if-group .sc-if-group{margin-left:16px;width:calc(100% - 16px)}
.sc-if-group .sc-block{margin-bottom:8px}
.sc-if-group .sc-block:last-child{margin-bottom:0}
.sc-if-group .sc-block.sc-if-branch{margin-left:16px;position:relative}
.sc-if-group .sc-block.sc-if-branch.sc-enter{animation:scIfBranchIn .32s var(--spring) both}
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
.sc-smart-menu-item{display:block;width:100%;text-align:left;border:0;background:transparent;color:var(--text);font:600 13px/1.35 inherit;padding:8px 10px;border-radius:10px;cursor:pointer;transition:background .14s ease-out,color .14s ease-out}
.sc-smart-menu-item.is-var{color:var(--text)}
.sc-smart-menu-head{padding:6px 10px 4px;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.05em;color:var(--dim);pointer-events:none;user-select:none}
.sc-smart-menu-item.sc-smart-menu-text{color:var(--dim);font-weight:600}
.sc-smart-menu-item.sc-smart-menu-clear{color:var(--muted);font-weight:600}
.sc-smart-menu-item.sc-smart-menu-clear:hover{color:#ff8a84;background:rgba(255,69,58,.12)}
.sc-smart-menu-item:hover,.sc-smart-menu-item:focus-visible{background:rgba(124,108,240,.2);outline:none}
.sc-smart-menu-sep{height:1px;margin:5px 8px;background:rgba(255,255,255,.09)}
.sc-block.sc-block-text{align-items:flex-start;padding-top:12px;padding-bottom:12px}
.sc-block.sc-block-text .sc-block-ico{align-self:flex-start;margin-top:1px}
.sc-block.sc-block-text .sc-block-body{flex-direction:column;align-items:stretch;gap:6px;min-width:0;padding-right:36px;padding-left:0}
.sc-block.sc-block-text .sc-del{top:14px;transform:none}
.sc-text-head{display:flex;align-items:center;min-height:24px;margin:0}
.sc-text-title{font-size:15px;font-weight:600;color:var(--text);letter-spacing:-.01em;line-height:1.35}
.sc-text-composer-wrap{width:100%;min-width:0;position:relative}
.sc-text-var-pop{position:fixed;z-index:160;display:none;flex-direction:row;flex-wrap:nowrap;align-items:center;gap:4px;padding:4px 6px;border-radius:10px;border:1px solid rgba(255,255,255,.1);background:rgba(22,22,28,.96);box-shadow:0 8px 28px rgba(0,0,0,.45);max-height:36px;overflow-x:auto;overflow-y:hidden;scrollbar-width:thin;pointer-events:auto}
.sc-text-var-pop::-webkit-scrollbar{height:4px}
.sc-text-var-pop::-webkit-scrollbar-thumb{background:rgba(255,255,255,.18);border-radius:999px}
.sc-text-var-bar{display:flex;flex-wrap:wrap;align-items:center;gap:5px;width:100%;flex:0 0 100%;padding:0 0 8px;margin:0 0 2px;border-bottom:1px solid rgba(255,255,255,.06)}
.sc-text-composer.sc-text-var-open{align-content:flex-start}
.sc-text-var-chip{display:inline-flex;align-items:center;gap:3px;border:0;border-radius:var(--sc-r-pill);padding:1px 6px 1px 5px;background:rgba(124,108,240,.2);color:var(--accent-hi);font:500 12px/1.3 inherit;cursor:pointer;white-space:nowrap;flex:0 0 auto;transition:background .12s ease-out,transform 80ms ease-out}
.sc-text-var-chip .sc-pill-icon-svg{width:12px;height:12px}
.sc-text-var-chip:hover{background:rgba(124,108,240,.32)}
.sc-text-var-chip:active{transform:scale(.96)}
.sc-text-composer{display:flex;flex-wrap:wrap;align-items:baseline;align-content:flex-start;gap:3px 0;width:100%;min-height:54px;padding:11px 12px;border-radius:var(--sc-r-inset);background:rgba(0,0,0,.24);border:1px solid rgba(255,255,255,.07);box-shadow:inset 0 1px 0 rgba(255,255,255,.04);line-height:1.5;font-size:18px;cursor:text;transition:border-color .15s var(--ease),background .15s var(--ease),box-shadow .15s var(--ease)}
.sc-text-composer:focus-within{border-color:rgba(124,108,240,.28);box-shadow:inset 0 1px 0 rgba(255,255,255,.04)}
.sc-text-parts{display:contents}
.sc-text-composer .sc-smart-field{vertical-align:middle}
.sc-text-composer .sc-pill.input{background:transparent;box-shadow:none;min-width:28px;width:auto;max-width:100%;height:auto;min-height:26px;line-height:1.45;padding:2px 2px;white-space:pre-wrap;overflow:visible;font-weight:500;color:var(--text)}
.sc-text-composer .sc-pill.input::placeholder{color:var(--muted);opacity:.65;font-weight:500}
.sc-text-composer .sc-pill.input:focus{background:rgba(124,108,240,.1);box-shadow:none;outline:none}
.sc-text-composer .sc-pill.var,.sc-text-composer .sc-pill.sc-smart-choice{min-height:26px;height:auto;font-size:15px;line-height:1.45;padding:2px 10px}
.sc-text-composer.sc-text-composer-single{padding:0;border:0;background:transparent;box-shadow:none;min-height:0}
.sc-text-composer.sc-text-composer-single:focus-within{box-shadow:none;border:0;background:transparent}
.sc-text-composer textarea.sc-text-segment-input{flex:0 1 auto;align-self:baseline;min-width:12px;max-width:100%;min-height:24px;max-height:240px;margin:0;padding:0;border:0;border-radius:0;background:transparent;box-shadow:none;color:var(--text);font:500 18px/1.5 inherit;resize:none;overflow-x:hidden;overflow-y:hidden;overflow-wrap:break-word;white-space:pre-wrap;outline:none;vertical-align:baseline;box-sizing:border-box}
.sc-text-composer textarea.sc-text-segment-input::placeholder{color:var(--muted);opacity:.65}
.sc-text-composer textarea.sc-text-segment-input:focus{background:transparent;outline:none}
.sc-text-composer .sc-text-var-inline{flex:0 0 auto;display:inline-flex;align-items:center;gap:3px;height:auto;margin:0;padding:0 5px 0 4px;border-radius:var(--sc-r-pill);background:var(--sc-pill-fill);color:var(--sc-pill-text);font:500 13px/1.35 inherit;vertical-align:baseline;user-select:none;box-sizing:border-box;cursor:pointer;position:relative;top:2px}
.sc-text-composer .sc-text-var-inline .sc-pill-icon-svg{width:13px;height:13px;flex:0 0 auto;opacity:.88}
.sc-text-composer .sc-text-var-inline .sc-pill-label{font-size:13px;line-height:1.35;font-weight:500}
.sc-text-composer .sc-text-var-inline .sc-pill-label{max-width:180px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sc-block-action .sc-block-ico.text{background:rgba(255,214,10,.18);color:#ffd60a}
.sc-pill:active{transform:scale(.97)}
.sc-pill.input{min-width:26px;width:26px;max-width:240px;height:30px;padding:0 12px;text-align:left;cursor:text;box-sizing:border-box;-webkit-appearance:none;appearance:none;line-height:30px;overflow:hidden;white-space:nowrap;font-weight:600;color:var(--sc-pill-text);background:var(--sc-pill-fill);box-shadow:none}
.sc-pill.input.is-truncated{text-overflow:ellipsis}
.sc-pill.input::placeholder{color:rgba(221,216,255,.55);opacity:1;font-weight:600}
.sc-pill.input:focus{background:rgba(124,108,240,.44);box-shadow:none;outline:none;transform:none;text-overflow:clip;overflow-x:auto;color:var(--sc-pill-text-nested)}
.sc-pill-measure{position:absolute;left:-9999px;top:0;visibility:hidden;white-space:pre;pointer-events:none;height:0;overflow:hidden}
.sc-drop-gap{height:52px;border-radius:var(--sc-r-block);border:2px dashed rgba(124,108,240,.55);background:rgba(124,108,240,.08);margin-bottom:12px;animation:scGapPulse .8s var(--spring) infinite alternate}
.sc-if-group .sc-drop-gap{margin-bottom:8px}
.sc-if-group .sc-drop-gap.sc-drop-gap-in-if{margin-left:16px;width:calc(100% - 16px)}
@keyframes scGapPulse{from{opacity:.55}to{opacity:1}}
.sc-block.sc-run-current{box-shadow:0 0 0 2px rgba(124,108,240,.9),0 10px 28px rgba(124,108,240,.26);background:rgba(124,108,240,.1)}
.sc-block.sc-run-waiting{isolation:isolate;overflow:hidden}
.sc-run-wait-sweep{position:absolute;left:0;top:0;bottom:0;width:0;z-index:0;pointer-events:none;background:linear-gradient(90deg,rgba(124,108,240,.42),rgba(124,108,240,.3));transition:width .12s linear}
.sc-block.sc-run-waiting>:not(.sc-run-wait-sweep):not(.sc-del){position:relative;z-index:1}
.sc-block.sc-run-waiting>.sc-del{z-index:2}
.sc-ed-canvas.sc-run-reflow .sc-block,.sc-ed-canvas.sc-run-reflow .sc-if-group,.sc-ed-canvas.sc-run-reflow .sc-run-result{transition:transform .4s var(--spring)}
.sc-run-result{position:relative;width:100%;margin:-2px 0 12px;padding:10px 18px 0;display:flex;flex-direction:column;align-items:center;box-sizing:border-box}
.sc-if-group .sc-run-result{margin-bottom:8px}
.sc-if-group .sc-block.sc-if-branch + .sc-run-result{margin-left:16px;width:calc(100% - 16px)}
.sc-run-result::before{content:"";position:absolute;left:50%;transform:translateX(-50%);top:-10px;height:20px;width:2px;border-radius:99px;background:linear-gradient(180deg,rgba(124,108,240,.58),rgba(90,164,240,.42));pointer-events:none;z-index:0}
.sc-run-result.sc-enter{animation:scRunResultIn .42s var(--spring) both}
@keyframes scRunResultIn{from{opacity:0;transform:translateY(-14px) scale(.97)}to{opacity:1;transform:none}}
.sc-run-result-box{position:relative;z-index:1;display:inline-block;max-width:min(100%,420px);padding:9px 13px;border-radius:var(--sc-r-control);background:rgba(255,255,255,.05);box-shadow:inset 0 0 0 1px rgba(255,255,255,.09);font:600 14px/1.35 inherit;color:var(--text);overflow-wrap:anywhere}
.sc-run-result-box em{font-style:normal;color:var(--muted)}
.sc-drawer{--sc-drawer-t:0;position:absolute;z-index:3;left:0;right:auto;bottom:0;width:100%;height:min(78%,520px);background:var(--sc-surface-inset);border-top:1px solid rgba(255,255,255,.08);border-radius:var(--sc-r-drawer) var(--sc-r-drawer) 0 0;box-shadow:none;display:flex;flex-direction:column;overflow:clip;transform:translateY(10%);transition:transform .38s var(--spring),height .38s var(--spring),width .38s var(--spring),left .38s var(--spring),bottom .38s var(--spring),border-radius .38s var(--spring),box-shadow .38s var(--spring);will-change:transform,width,height,left;touch-action:none}
.sc-drawer.dragging{transition:none}
.sc-drawer-chips,.sc-drawer-list{opacity:calc(1 - var(--sc-drawer-t));pointer-events:none;transition:opacity .28s var(--ease)}
.sc-drawer:not(.sc-drawer-capsule) .sc-drawer-chips,.sc-drawer:not(.sc-drawer-capsule) .sc-drawer-list{pointer-events:auto}
.sc-drawer.sc-drawer-capsule .sc-drawer-grab{padding:6px 0 2px}
.sc-drawer.sc-drawer-capsule .sc-drawer-search-wrap{margin:0 12px 4px;padding:0 10px;gap:6px}
.sc-drawer.sc-drawer-capsule .sc-drawer-search-wrap .sc-drawer-search{padding:9px 0}
.sc-drawer.sc-drawer-capsule .sc-drawer-search-chip.is-on{padding:2px 8px;font-size:11px;line-height:1.2;gap:4px}
.sc-drawer.sc-drawer-capsule .sc-drawer-search-chip .sc-drawer-filter-ico{width:11px;height:11px}
.sc-drawer.sc-drawer-capsule .sc-drawer-chips,.sc-drawer.sc-drawer-capsule .sc-drawer-list{display:none}
.sc-drawer-toolbar{display:flex;align-items:center;justify-content:space-between;gap:6px;flex:0 0 auto;max-height:calc(var(--sc-drawer-t) * 54px);padding:calc(var(--sc-drawer-t) * 2px) calc(var(--sc-drawer-t) * 18px) calc(var(--sc-drawer-t) * 12px);overflow:hidden;opacity:var(--sc-drawer-t);pointer-events:none;transition:max-height .34s var(--spring),padding .34s var(--spring),opacity .28s var(--ease)}
.sc-drawer.sc-drawer-capsule .sc-drawer-toolbar{max-height:40px;padding:0 14px 4px;opacity:1;pointer-events:auto}
.sc-tb-btn{width:40px;height:40px;border:0;border-radius:50%;background:transparent;color:var(--dim);padding:0;display:grid;place-items:center;transition:background .14s var(--ease),color .14s var(--ease),transform 80ms ease-out}
.sc-tb-btn:not(:disabled){cursor:pointer}
.sc-tb-btn svg{width:21px;height:21px;display:block;flex:0 0 auto;pointer-events:none}
.sc-tb-btn:hover:not(:disabled){background:rgba(255,255,255,.08);color:#fff}
.sc-tb-btn:active:not(:disabled){transform:scale(.9)}
.sc-tb-btn:disabled,.sc-tb-btn:disabled:hover,.sc-tb-btn:disabled:active{opacity:.32;cursor:default;background:transparent;transform:none}
.sc-tb-btn.sc-tb-play{color:var(--text)}
.sc-drawer-grab{padding:12px 0 8px;cursor:grab;touch-action:none;flex:0 0 auto}
.sc-drawer-grab:active{cursor:grabbing}
.sc-drawer-handle{width:36px;height:5px;border-radius:99px;background:rgba(255,255,255,.32);margin:0 auto;pointer-events:none;transition:background .18s var(--ease),width .18s var(--ease)}
.sc-drawer-grab:hover .sc-drawer-handle{background:rgba(255,255,255,.45);width:42px}
.sc-drawer-search-wrap{display:flex;align-items:center;gap:8px;margin:0 16px 10px;padding:0 14px;border-radius:999px;background:rgba(255,255,255,.06);box-shadow:inset 0 0 0 1px rgba(255,255,255,.08);flex:0 0 auto;transition:background .15s var(--ease),box-shadow .15s var(--ease)}
.sc-drawer-search-wrap:focus-within{background:rgba(255,255,255,.1);box-shadow:inset 0 0 0 1px rgba(124,108,240,.35),0 0 0 3px rgba(124,108,240,.12)}
.sc-drawer-search-icon{width:18px;height:18px;flex:0 0 auto;color:var(--muted);display:grid;place-items:center;pointer-events:none}
.sc-drawer-search-icon svg{width:16px;height:16px;display:block;opacity:.85}
.sc-drawer-search-chip{display:none;align-items:center;gap:5px;padding:4px 10px;border-radius:999px;background:rgba(48,48,52,.92);color:#fff;font:700 13px/1 inherit;flex-shrink:0;white-space:nowrap}
.sc-drawer-search-chip.is-on{display:inline-flex}
.sc-drawer-search-wrap .sc-drawer-search{flex:1;min-width:0;margin:0;padding:11px 0;border:0;border-radius:0;background:transparent;font:15px/1.35 inherit;color:var(--text);outline:none;box-shadow:none;-webkit-appearance:none;appearance:none}
.sc-drawer-search-wrap .sc-drawer-search:focus{background:transparent;box-shadow:none}
.sc-drawer-search-wrap .sc-drawer-search::-webkit-search-decoration,.sc-drawer-search-wrap .sc-drawer-search::-webkit-search-cancel-button{display:none;-webkit-appearance:none}
.sc-drawer-search::placeholder{color:var(--muted)}
.sc-drawer-chips{display:flex;gap:8px;padding:0 16px 10px;overflow:auto;flex:0 0 auto}
.sc-drawer-filter-pick{display:inline-flex;align-items:center;gap:6px;border:1px solid rgba(124,108,240,.38);border-radius:999px;background:rgba(124,108,240,.1);color:var(--accent-hi);font:600 13px/1 inherit;padding:7px 14px;cursor:pointer;white-space:nowrap;transition:background .12s ease-out,border-color .12s ease-out,transform 80ms ease-out}
.sc-drawer-filter-pick .ico{display:inline-flex;align-items:center;justify-content:center;width:17px;height:17px;flex-shrink:0;color:currentColor}
.sc-drawer-filter-ico{width:16px;height:16px;display:block}
.sc-drawer-search-chip .ico{display:inline-flex;align-items:center;justify-content:center;color:#fff}
.sc-drawer-search-chip .sc-drawer-filter-ico{width:13px;height:13px}
.sc-drawer-filter-pick:hover{background:rgba(124,108,240,.18);border-color:rgba(124,108,240,.52)}
.sc-drawer-filter-pick:active{transform:scale(.96)}
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
.sc-catalog-ico{width:34px;height:34px;border-radius:var(--sc-r-icon);display:grid;place-items:center;background:rgba(124,108,240,.18);font-size:15px;flex:0 0 auto;color:inherit}
.sc-catalog-ico .sc-block-icon-svg{width:18px;height:18px}
.sc-catalog-ico.chat{background:rgba(52,199,89,.18);color:#34c759}
.sc-catalog-ico.action{background:rgba(124,108,240,.22);color:#b8afff}
.sc-catalog-ico.wait{background:rgba(255,159,10,.18);color:#ff9f0a}
.sc-catalog-ico.connect{background:rgba(90,164,240,.18);color:#5aa4f0}
.sc-catalog-ico.flow,.sc-catalog-ico.text{background:rgba(100,210,255,.16);color:#64d2ff}
.sc-catalog-ico.clip{background:rgba(255,186,120,.16);color:#ffb86c}
.sc-block-action .sc-block-ico.clip{background:rgba(255,186,120,.18);color:#ffb86c}
.sc-block-action .sc-block-ico.loop,.sc-catalog-ico.loop{background:rgba(160,170,255,.16);color:#aeb8ff}
.sc-block-action .sc-block-ico.play,.sc-catalog-ico.play{background:rgba(255,120,180,.16);color:#ff8cc8}
.sc-block.sc-repeat-foot .sc-block-body,.sc-block.sc-if-foot .sc-block-body{padding-right:22px}
.sc-catalog-ico.stop{background:rgba(255,69,58,.16);color:#ff453a}
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
.sc-if-cond-add{width:30px;height:30px;border:0;border-radius:50%;background:rgba(10,132,255,.22);color:#409cff;font:700 20px/1 inherit;cursor:pointer;padding:0;flex:0 0 auto;align-self:center;transition:background .12s ease-out,transform 80ms ease-out}
.sc-if-cond-add:hover{background:rgba(10,132,255,.38);color:#fff}
.sc-if-cond-add:active{transform:scale(.92)}
.sc-if-multi-wrap{display:flex;flex-direction:column;align-items:stretch;width:100%;min-width:0}
.sc-if-multi-head{display:flex;flex-wrap:wrap;align-items:center;gap:4px 6px}
.sc-block-body.sc-if-multi-body{flex-direction:column;align-items:stretch;padding-right:36px;min-height:0}
.sc-block.sc-if-head .sc-block-body{align-items:stretch}
.sc-block.sc-if-head .sc-del{top:14px;transform:none}
.sc-block.sc-if-head .sc-del:active{transform:scale(.92)}
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
.sc-pop.sc-stepper-pop .sc-smart-menu-inner{display:flex;align-items:stretch;padding:4px;min-width:112px}
.sc-stepper-btn{flex:1;min-width:48px;padding:10px 14px;border:0;border-radius:12px;background:transparent;color:var(--text);font:700 20px/1 inherit;cursor:pointer;transition:background .12s ease-out,transform 80ms ease-out}
.sc-stepper-btn:hover{background:rgba(124,108,240,.2)}
.sc-stepper-btn:active{transform:scale(.94);background:rgba(124,108,240,.28)}
.sc-stepper-sep{width:1px;margin:6px 0;background:rgba(255,255,255,.1);flex:0 0 auto}
.sc-stepper-pill{cursor:pointer}
@supports (corner-shape:squircle){
  #play,#sc-editor,.sc-block,.sc-block-filters,.sc-drawer,.sc-drawer-search-wrap,.sc-drawer-search-chip,.sc-drawer-filter-pick,.sc-chip,.sc-catalog,.sc-tile,.sc-tile-ico,.sc-pop,.sc-ico,.sc-drag-ghost,.sc-drop-gap,.sc-pill,.sc-sender-chip,.sc-sender-add,.sc-smart-menu-inner,.sc-smart-menu-item,.sc-run-result,.sc-run-result-box,.sc-text-var-chip,.sc-text-composer .sc-text-var-inline,.modal-box,.opt{corner-shape:squircle}
  .sc-ed-back,.sc-ed-icon,.sc-ed-delete,.sc-ed-save{corner-shape:round;border-radius:50%}
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
.confirm-files{display:flex;flex-direction:column;gap:5px;margin-top:13px;max-height:min(240px,45vh);overflow-y:auto;padding:9px 10px;border:1px solid var(--line);border-radius:9px;background:rgba(0,0,0,.18)}
.confirm-file{color:var(--dim);font-size:12px;line-height:1.35;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.confirm-actions{display:flex;justify-content:flex-end;gap:9px;margin-top:22px}

:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list,.confirm-files){
  scrollbar-width:thin;scrollbar-color:rgba(255,255,255,.2) transparent;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list,.confirm-files)::-webkit-scrollbar{
  width:10px;height:10px;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list,.confirm-files)::-webkit-scrollbar-track{
  background:transparent;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list,.confirm-files)::-webkit-scrollbar-thumb{
  min-height:36px;border:3px solid transparent;border-radius:999px;
  background:rgba(255,255,255,.18);background-clip:padding-box;
}
:is(.settings-pane,.backup-list,.account-view,.onboard-box,#play-hint-flyout-inner,#alert-flyout-inner,#fr-list,.confirm-files)::-webkit-scrollbar-thumb:hover{
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

  <section id="shortcuts-view" aria-hidden="true" data-sc-lib="shortcuts">
    <div class="sc-lib-panels">
    <div class="sc-lib-panel on" id="sc-panel-shortcuts" role="tabpanel">
      <div class="sc-page-head">
        <h1>Shortcuts (Beta)</h1>
      </div>
      <div class="sc-section">Personal</div>
      <div class="sc-panel-body sc-shortcuts-card">
        <div class="sc-gallery-search-wrap">
          <span class="sc-gallery-search-icon" aria-hidden="true">
            <svg viewBox="0 0 24 24" fill="none"><circle cx="11" cy="11" r="6.5" stroke="currentColor" stroke-width="1.8"/><path d="M16 16l4.5 4.5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
          </span>
          <input class="sc-gallery-search" id="sc-gallery-search" type="search" placeholder="Search" autocomplete="off">
        </div>
        <div class="sc-gallery-grid" id="sc-gallery-grid"></div>
      </div>
    </div>
    <div class="sc-lib-panel" id="sc-panel-automation" role="tabpanel">
      <div class="sc-page-head"><h1>Automation (Beta)</h1></div>
      <div class="sc-section">Personal</div>
      <div class="sc-panel-body sc-list-body" id="sc-list"></div>
    </div>
    </div>
    <div class="sc-bottom-dock">
      <div class="sc-bottom-dock-inner">
        <div class="sc-lib-tabs" role="tablist">
          <span class="sc-lib-tabs-indicator" aria-hidden="true"></span>
          <button class="sc-lib-tab on" type="button" data-sc-lib="shortcuts" role="tab" aria-selected="true">Shortcuts</button>
          <button class="sc-lib-tab" type="button" data-sc-lib="automation" role="tab" aria-selected="false">Automation</button>
        </div>
        <button class="sc-fab" id="sc-fab-manual" type="button" title="Create shortcut">+</button>
        <button class="sc-fab" id="sc-fab-auto" type="button" title="Create automation">+</button>
      </div>
    </div>
    <div id="sc-editor" aria-hidden="true">
      <div class="sc-ed-head">
        <button class="sc-ed-back" id="sc-ed-back" type="button" title="Back">
          <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M14.5 7.5L9 12l5.5 4.5" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </button>
        <div class="sc-ed-head-center" id="sc-ed-head-center">
          <button type="button" class="sc-ed-title-trigger" id="sc-ed-title-trigger" aria-haspopup="menu" aria-expanded="false" aria-controls="sc-ed-title-menu">
            <span class="sc-ed-title-tile" id="sc-ed-title-tile" aria-hidden="true"></span>
            <span class="sc-ed-title-text" id="sc-ed-title-label">New Shortcut</span>
            <span class="sc-ed-title-chev" aria-hidden="true"><span class="sc-ed-chev-svg"></span></span>
          </button>
          <input class="sc-ed-title-input" id="sc-ed-title" type="text" placeholder="New Shortcut" maxlength="64" autocomplete="off" spellcheck="false">
        </div>
        <div class="sc-ed-trailing">
          <button class="sc-ed-icon sc-ed-delete" id="sc-ed-delete" type="button" title="Delete">
            <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M9 4h6M10 4V3a1 1 0 011-1h2a1 1 0 011 1v1M6 7h12M7 7v12a2 2 0 002 2h6a2 2 0 002-2V7M10 11v5M14 11v5" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
          </button>
          <button class="sc-ed-icon sc-ed-save" id="sc-ed-save" type="button" title="Save">
            <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M7.5 12.5l3.5 3.5 8-8.5" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"/></svg>
          </button>
        </div>
      </div>
      <div class="sc-ed-body">
        <div class="sc-ed-canvas" id="sc-ed-canvas"></div>
        <div id="sc-text-var-pop" class="sc-text-var-pop" role="toolbar" aria-label="Insert variable" aria-hidden="true"></div>
        <div class="sc-drawer" id="sc-drawer">
          <div class="sc-drawer-grab" id="sc-drawer-grab"><div class="sc-drawer-handle" id="sc-drawer-handle"></div></div>
          <div class="sc-drawer-search-wrap" id="sc-drawer-search-wrap">
            <span class="sc-drawer-search-icon" aria-hidden="true"><svg viewBox="0 0 24 24" fill="none"><circle cx="11" cy="11" r="6.2" stroke="currentColor" stroke-width="2"/><path d="M16 16l4.5 4.5" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg></span>
            <span class="sc-drawer-search-chip" id="sc-drawer-search-chip"></span>
            <input class="sc-drawer-search" id="sc-drawer-search" type="text" autocomplete="off" placeholder="Search actions" enterkeyhint="search">
          </div>
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
              <div class="row-actions">
                <button class="secondary" id="backup-select-all" type="button">Select all</button>
                <button class="primary" id="backup-upload" type="button">Upload</button>
              </div>
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
              <div class="row-actions">
                <button class="secondary" id="backup-restore-select-all" type="button">Select all</button>
                <button class="primary" id="backup-restore-selected" type="button">Restore</button>
              </div>
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
  } else if (e.key === "Enter" && !$("play").disabled) {
    var ed = $("sc-editor");
    var inEditor = ed && ed.style.display === "flex" && ed.classList.contains("on");
    if (inEditor) return;
    if (e.target.closest("input,textarea,button,form,[contenteditable='true']")) return;
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
  detailItems.forEach(function (detail) {
    var row = document.createElement("div");
    row.className = "confirm-file";
    row.textContent = detail;
    files.appendChild(row);
  });
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
  var statusOption = e.target.closest("[data-filter-status]");
  if (statusOption) {
    var status = statusOption.dataset.filterStatus;
    if (status === "all") backupFilterUnsavedOnly = false;
    else if (status === "unsaved") backupFilterUnsavedOnly = !backupFilterUnsavedOnly;
    renderBackupLocal();
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
  var restoreStatusOption = e.target.closest("[data-restore-filter-status]");
  if (restoreStatusOption) {
    var restoreStatus = restoreStatusOption.dataset.restoreFilterStatus;
    if (restoreStatus === "all") restoreFilterMissingLocalOnly = false;
    else if (restoreStatus === "missing_local") restoreFilterMissingLocalOnly = !restoreFilterMissingLocalOnly;
    renderBackupServer();
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
$("backup-select-all").addEventListener("click", function () {
  selectAllFilteredBackupUploads();
});
$("backup-upload").addEventListener("click", function () {
  var paths = Object.keys(backupSelectedUploads);
  if (paths.length) send({cmd: "backupUpload", paths: paths});
});
$("backup-restore-select-all").addEventListener("click", function () {
  selectAllFilteredRestores();
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
var backupFilterUnsavedOnly = false;
var restoreFilterMissingLocalOnly = false;
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
function backupSavedPathSet() {
  var set = Object.create(null);
  if (!backupRenderState) return set;
  (backupRenderState.backupVersions || []).forEach(function (version) {
    set[backupPathKey(version.path)] = true;
  });
  return set;
}
function backupLocalPathSet() {
  var set = Object.create(null);
  if (!backupRenderState) return set;
  (backupRenderState.backupFiles || []).forEach(function (file) {
    set[backupPathKey(file.path)] = true;
  });
  return set;
}
function filterBackupLocalFiles(files) {
  var savedPaths = backupSavedPathSet();
  return files.filter(function (file) {
    var hasTypeFilters = Object.keys(backupTypeFilters).length > 0;
    var typeMatches = !hasTypeFilters || !!backupTypeFilters[backupExtension(file.path)];
    var folder = backupTopFolder(file.path).toLowerCase();
    var hasFolderFilters = Object.keys(backupFolderFilters).length > 0;
    var folderMatches = !hasFolderFilters || !!backupFolderFilters[folder];
    if (backupFolderExcluded(folder) || !typeMatches || !folderMatches)
      return false;
    if (backupFilterUnsavedOnly && savedPaths[backupPathKey(file.path)])
      return false;
    return true;
  });
}
function filterRestoreVersions(versions) {
  var localPaths = backupLocalPathSet();
  return versions.filter(function (version) {
    var folder = backupTopFolder(version.path).toLowerCase();
    var hasTypeFilters = Object.keys(restoreTypeFilters).length > 0;
    var typeMatches = !hasTypeFilters || !!restoreTypeFilters[backupExtension(version.path)];
    var hasFolderFilters = Object.keys(restoreFolderFilters).length > 0;
    var folderMatches = !hasFolderFilters || !!restoreFolderFilters[folder];
    if (!typeMatches || !folderMatches)
      return false;
    if (restoreFilterMissingLocalOnly && localPaths[backupPathKey(version.path)])
      return false;
    return true;
  });
}
function selectAllFilteredBackupUploads() {
  if (backupDisabled()) return;
  filterBackupLocalFiles(backupRenderState.backupFiles || []).forEach(function (file) {
    backupSelectedUploads[file.path] = true;
  });
  renderBackupLocal();
}
function selectAllFilteredRestores() {
  if (backupDisabled()) return;
  var allVersions = (backupRenderState.backupVersions || []).filter(function (version) {
    return !backupFolderExcluded(backupTopFolder(version.path));
  });
  var taken = Object.create(null);
  allVersions.forEach(function (version) {
    if (backupSelectedRestores[version.id])
      taken[backupPathKey(version.path)] = true;
  });
  filterRestoreVersions(allVersions).forEach(function (version) {
    var key = backupPathKey(version.path);
    if (taken[key]) return;
    backupSelectedRestores[version.id] = true;
    taken[key] = true;
  });
  renderBackupServer();
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
  var statusHtml = '<div class="backup-filter-title">Status</div>' +
    '<button class="backup-filter-option' + (!backupFilterUnsavedOnly ? " on" : "") +
    '" type="button" data-filter-status="all">All files</button>' +
    '<button class="backup-filter-option' + (backupFilterUnsavedOnly ? " on" : "") +
    '" type="button" data-filter-status="unsaved">Files not saved</button>';
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
  $("backup-filter-menu").innerHTML = statusHtml + typeHtml + folderHtml;
  $("backup-filter-button").classList.toggle("filtered", backupFilterUnsavedOnly || hasTypeFilters || hasFolderFilters);
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
  var statusHtml = '<div class="backup-filter-title">Status</div>' +
    '<button class="backup-filter-option' + (!restoreFilterMissingLocalOnly ? " on" : "") +
    '" type="button" data-restore-filter-status="all">All versions</button>' +
    '<button class="backup-filter-option' + (restoreFilterMissingLocalOnly ? " on" : "") +
    '" type="button" data-restore-filter-status="missing_local">Files not on this computer</button>';
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
  $("restore-filter-menu").innerHTML = statusHtml + typeHtml + folderHtml;
  $("restore-filter-button").classList.toggle("filtered", restoreFilterMissingLocalOnly || hasTypeFilters || hasFolderFilters);
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
  $("backup-select-all").disabled = disabled;
  $("backup-restore-select-all").disabled = disabled;
  $("backup-refresh").disabled = disabled;
  $("backup-restore-count").textContent = "";
  $("backup-upload").textContent = uploadCount ? "Upload (" + uploadCount + ")" : "Upload";
  $("backup-restore-selected").textContent = restoreCount ? "Restore (" + restoreCount + ")" : "Restore";
}
function renderBackupLocal() {
  if (!backupRenderState) return;
  var disabled = backupDisabled();
  renderBackupFilterMenu();
  var files = filterBackupLocalFiles(backupRenderState.backupFiles || []);
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
  var versions = filterRestoreVersions(allVersions);
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
var scLibraryTab = "shortcuts";
var scEditKind = "automation";
var scGallerySearch = "";
var scEditing = null;
var scDrawerFilter = null;
var scPopEl = null;
var SC_TILE_TONES = ["tone-brown", "tone-slate", "tone-green", "tone-blue", "tone-purple", "tone-rose"];
var SC_PLAY_ICON = '<svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path fill="currentColor" d="M8.05 5.55a1.35 1.35 0 0 1 2.09-1.12l8.31 5.54a1.35 1.35 0 0 1 0 2.24l-8.31 5.54a1.35 1.35 0 0 1-2.09-1.12V5.55z"/></svg>';

var SC_TRIGGERS = [{
  id: "chat_received", category: "communication", title: "Chat message",
  whenHint: "When a chat message is received from others", icon: "\u2709", tone: "chat",
  defaults: {type: "chat_received", channel: "all", filters: []}
}, {
  id: "server_connect", category: "connection", title: "Server connect",
  whenHint: "When connecting to a specific server", icon: "\u25CE", tone: "connect",
  defaults: {type: "server_connect", targets: []}
}];
var SC_GET_OS_PROPERTIES = [
  {value: "foreground_window_title", label: "Foreground Window Title", varId: "foreground_window_title"},
  {value: "game_window_focused", label: "Game Window Focused", varId: "game_window_focused"}
];
var SC_GET_GAME_PROPERTIES = [
  {value: "connected", label: "Connected", varId: "connected"},
  {value: "server_name", label: "Server Name", varId: "server_name"},
  {value: "map", label: "Map", varId: "map"},
  {value: "server_address", label: "Server Address", varId: "server_address"},
  {value: "my_name", label: "My Name", varId: "my_name"},
  {value: "nearest_player", label: "Nearest Player", varId: "nearest_player"}
];
var SC_ACTIONS = [
  {id: "get_os_detail", category: "flow", title: "Get OS Detail", hint: "Read window focus and other OS state into a variable", icon: "\u2193", tone: "flow", defaults: {type: "get", property: "foreground_window_title"}},
  {id: "get_game_detail", category: "flow", title: "Get Game Detail", hint: "Read server, map, and player state into a variable", icon: "\u2193", tone: "connect", defaults: {type: "get", property: "connected"}},
  {id: "get_clipboard", category: "flow", title: "Get Clipboard", hint: "Read plain text from the system clipboard", icon: "\u2193", tone: "clip", defaults: {type: "get_clipboard", as: "clipboard"}},
  {id: "text", category: "flow", title: "Text", hint: "Combine variables and text into one value", icon: "\u270D", tone: "text", defaults: {type: "text", parts: [{mode: "text", text: ""}], as: "text"}},
  {id: "repeat", category: "flow", title: "Repeat", hint: "Run actions inside the loop several times", icon: "\u21bb", tone: "loop", defaults: {type: "repeat", count: 1}},
  {id: "end_repeat", category: "flow", title: "End Repeat", hint: "End a Repeat block", icon: "\u21bb", tone: "loop", defaults: {type: "end_repeat"}},
  {id: "if", category: "flow", title: "If", hint: "Continue only when a condition matches", icon: "\u2442", tone: "flow", defaults: {type: "if", left: "", op: "contains", right: ""}},
  {id: "otherwise", category: "flow", title: "Otherwise", hint: "Run when the If condition did not match", icon: "\u2443", tone: "flow", defaults: {type: "otherwise"}},
  {id: "end_if", category: "flow", title: "End If", hint: "End an If block", icon: "\u2444", tone: "flow", defaults: {type: "end_if"}},
  {id: "stop", category: "flow", title: "Stop", hint: "Stop running this shortcut", icon: "\u25A0", tone: "stop", defaults: {type: "stop"}},
  {id: "run_shortcut", category: "flow", title: "Run Shortcut", hint: "Run another shortcut from your library", icon: "\u25b6", tone: "play", defaults: {type: "run_shortcut", shortcutId: ""}},
  {id: "connect_server", category: "connection", title: "Connect to server", hint: "Connect to an IP or IP:port", icon: "\u25CE", tone: "connect", defaults: {type: "connect_server", address: "127.0.0.1:8303"}},
  {id: "leave_server", category: "connection", title: "Leave server", hint: "Disconnect from the current server", icon: "\u21AA", tone: "connect", defaults: {type: "leave_server"}},
  {id: "send_chat", category: "actions", title: "Send message", hint: "Send a variable or text to chat", icon: "\u2709", tone: "chat", defaults: {type: "send_chat", channelMode: "text", channel: "all", uclientRoomMode: "text", uclientRoomId: "", messageMode: "text", messageText: ""}},
  {id: "wait", category: "actions", title: "Wait", hint: "Pause before the next step", icon: "\u23f1", tone: "wait", defaults: {type: "wait", seconds: 1}},
  {id: "switch_weapon_use", category: "actions", title: "Use weapon", hint: "Switch weapon and use it", icon: "\u2692", tone: "action", defaults: {type: "switch_weapon_use", weapon: "hammer"}},
  {id: "switch_weapon", category: "actions", title: "Switch weapon", hint: "Switch weapon without using it", icon: "\u2692", tone: "action", defaults: {type: "switch_weapon", weapon: "hammer"}},
  {id: "emote", category: "actions", title: "Emote", hint: "Show an eye emote on your tee", icon: "\u263a", tone: "action", defaults: {type: "emote", emote: "normal"}},
  {id: "kill", category: "actions", title: "Kill", hint: "Kill your tee (respawn)", icon: "\u2620", tone: "action", defaults: {type: "kill"}},
  {id: "vote", category: "actions", title: "Vote", hint: "Vote yes or no on an active vote", icon: "\u2713", tone: "action", defaults: {type: "vote", choice: "yes"}},
  {id: "set_skin", category: "actions", title: "Set skin", hint: "Change player or dummy skin", icon: "\u2728", tone: "action", defaults: {type: "set_skin", target: "player", skin: "default"}},
  {id: "set_custom_color", category: "actions", title: "Custom colors", hint: "Toggle custom colors on or off", icon: "\u25cf", tone: "action", defaults: {type: "set_custom_color", target: "player", enabled: true}},
  {id: "set_body_color", category: "actions", title: "Body color", hint: "Set body color", icon: "\u25cf", tone: "action", defaults: {type: "set_body_color", target: "player", color: 0}},
  {id: "set_feet_color", category: "actions", title: "Feet color", hint: "Set feet color", icon: "\u25cf", tone: "action", defaults: {type: "set_feet_color", target: "player", color: 0}},
  {id: "set_name", category: "actions", title: "Set name", hint: "Change player or dummy name", icon: "\u270e", tone: "action", defaults: {type: "set_name", target: "player", name: "name"}}
];
var SC_TRIGGER_SECTIONS = [{id: "communication", label: "Communication"}, {id: "connection", label: "Connection"}];
var SC_ACTION_SECTIONS = [{id: "flow", label: "Flow"}, {id: "connection", label: "Connection"}, {id: "actions", label: "Actions"}];
var SC_DRAWER_FILTERS = [
  {id: "automation", label: "Automation", automationOnly: true},
  {id: "scripting", label: "Scripting"},
  {id: "connection", label: "Connection"},
  {id: "actions", label: "Actions"}
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
function scActionDefForAction(data) {
  if (!data) return scActionDef("wait");
  if (data.type === "get") {
    return scGetPropertyGroup(data.property) === "game" ? scActionDef("get_game_detail") : scActionDef("get_os_detail");
  }
  return scActionDef(data.type);
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
  return scNormalizeTrigger(JSON.parse(JSON.stringify(t)));
}
function scCleanTriggerForSave(t) {
  t = scNormalizeTrigger(JSON.parse(JSON.stringify(t)));
  if (t.type === "server_connect") {
    t.targets = (t.targets || []).filter(function (n) { return n; });
    return t;
  }
  if (t.type === "chat_received") {
    return {type: "chat_received", channel: "all", filters: []};
  }
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
  if (t.type === "chat_received") {
    return {type: "chat_received", channel: "all", filters: []};
  }
  return t;
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
  if (t.type === "chat_received") return "When a chat message is received from others";
  return "When something happens";
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
function scDrawerFilterSvg(filterId) {
  var sw = "1.85";
  var rnd = ' stroke-linecap="round" stroke-linejoin="round"';
  function wrap(inner) {
    return '<svg class="sc-drawer-filter-ico" viewBox="0 0 24 24" fill="none" aria-hidden="true">' + inner + '</svg>';
  }
  if (filterId === "automation") {
    return wrap('<circle cx="12" cy="12" r="8.5" stroke="currentColor" stroke-width="' + sw + '"/><path d="M12 7.6V12l3.1 2.3" stroke="currentColor" stroke-width="' + sw + '"' + rnd + '/>');
  }
  if (filterId === "scripting") {
    return wrap('<path d="M8 7.2c-2.1 0-3.4 1.35-3.4 3.1v3.4c0 1.75 1.3 3.1 3.4 3.1M16 7.2c2.1 0 3.4 1.35 3.4 3.1v3.4c0 1.75-1.3 3.1-3.4 3.1" stroke="currentColor" stroke-width="' + sw + '"' + rnd + '/><path d="M10.4 10.2l-1.1 1.8 1.1 1.8M13.6 10.2l1.1 1.8-1.1 1.8" stroke="currentColor" stroke-width="' + sw + '"' + rnd + '/>');
  }
  if (filterId === "connection") {
    return wrap('<circle cx="12" cy="12" r="3.1" stroke="currentColor" stroke-width="' + sw + '"/><circle cx="12" cy="12" r="7.1" stroke="currentColor" stroke-width="' + sw + '" stroke-dasharray="3.2 3.2"/>');
  }
  return wrap('<rect x="5.4" y="5.4" width="5.6" height="5.6" rx="1.3" stroke="currentColor" stroke-width="' + sw + '"/><rect x="13" y="13" width="5.6" height="5.6" rx="1.3" stroke="currentColor" stroke-width="' + sw + '"/><path d="M10.9 8.2h2.6v5.1M8.2 10.9h5.1v2.6" stroke="currentColor" stroke-width="' + sw + '"' + rnd + '/>');
}
function scDrawerFilterDef(id) {
  for (var i = 0; i < SC_DRAWER_FILTERS.length; i++) {
    if (SC_DRAWER_FILTERS[i].id === id) return SC_DRAWER_FILTERS[i];
  }
  return null;
}
function scDrawerTriggerSections() {
  if (scIsManualEdit()) return [];
  if (scDrawerFilter !== "automation") return [];
  return SC_TRIGGER_SECTIONS;
}
function scDrawerActionSections() {
  var f = scDrawerFilter;
  if (!f) return SC_ACTION_SECTIONS;
  if (f === "automation") return [];
  if (f === "scripting") return SC_ACTION_SECTIONS.filter(function (s) { return s.id === "flow"; });
  if (f === "connection") return SC_ACTION_SECTIONS.filter(function (s) { return s.id === "connection"; });
  if (f === "actions") return SC_ACTION_SECTIONS.filter(function (s) { return s.id === "actions"; });
  return [];
}
function scCatalogRow(kind, def) {
  var isTrigger = kind === "trigger";
  var title = def.title || def.label || def.id;
  var sub = isTrigger ? (def.whenHint || scTriggerWhenPreview(def.defaults)) : (def.hint || def.label || title);
  var tone = def.tone || (isTrigger ? "chat" : "action");
  return '<div class="sc-catalog" data-add-kind="' + kind + '" data-add-id="' + esc(def.id) + '">' +
    '<span class="sc-catalog-ico ' + tone + '">' + scCatalogBlockIcon(def, kind) + '</span>' +
    '<div class="sc-catalog-text"><b>' + esc(title) + '</b><small>' + esc(sub) + '</small></div></div>';
}
function scSummaryTrigger(t) {
  return scTriggerWhenPreview(t);
}
function scGetOsPropertyOptions() {
  return SC_GET_OS_PROPERTIES.map(function (p) { return {value: p.value, label: p.label}; });
}
function scGetGamePropertyOptions() {
  return SC_GET_GAME_PROPERTIES.map(function (p) { return {value: p.value, label: p.label}; });
}
function scFindGetPropertyMeta(prop) {
  var id = prop == null ? "" : String(prop);
  if (id === "window_active") id = "foreground_window_title";
  var i;
  for (i = 0; i < SC_GET_OS_PROPERTIES.length; i++) {
    if (SC_GET_OS_PROPERTIES[i].value === id) return SC_GET_OS_PROPERTIES[i];
  }
  for (i = 0; i < SC_GET_GAME_PROPERTIES.length; i++) {
    if (SC_GET_GAME_PROPERTIES[i].value === id) return SC_GET_GAME_PROPERTIES[i];
  }
  return null;
}
function scGetPropertyGroup(prop) {
  var meta = scFindGetPropertyMeta(prop);
  if (!meta) return "os";
  for (var i = 0; i < SC_GET_GAME_PROPERTIES.length; i++) {
    if (SC_GET_GAME_PROPERTIES[i].value === meta.value) return "game";
  }
  return "os";
}
function scGetPropertyVarId(prop) {
  var meta = scFindGetPropertyMeta(prop);
  return meta ? meta.varId : (prop || "value");
}
function scLabelGetProperty(prop) {
  var meta = scFindGetPropertyMeta(prop);
  return meta ? meta.label : (prop || "Value");
}
function scNormalizeGetProperty(prop, groupHint) {
  var id = prop == null ? "" : String(prop);
  if (id === "window_active") id = "foreground_window_title";
  if (scFindGetPropertyMeta(id)) return id;
  if (groupHint === "game" && SC_GET_GAME_PROPERTIES.length) return SC_GET_GAME_PROPERTIES[0].value;
  return SC_GET_OS_PROPERTIES.length ? SC_GET_OS_PROPERTIES[0].value : "foreground_window_title";
}
function scLabelEmote(e) {
  var m = {normal: "Normal", happy: "Happy", angry: "Angry", pain: "Pain", surprise: "Surprise", blink: "Blink"};
  return m[e] || "Normal";
}
function scNormalizeEmote(e) {
  var id = e == null ? "" : String(e);
  var ok = ["normal", "happy", "angry", "pain", "surprise", "blink"];
  return ok.indexOf(id) >= 0 ? id : "normal";
}
function scLabelVoteChoice(c) {
  return c === "no" ? "No" : "Yes";
}
function scLabelYesNoValue(v) {
  return String(v == null ? "" : v).toLowerCase() === "no" ? "No" : "Yes";
}
function scNormalizeYesNoValue(v) {
  return String(v == null ? "" : v).toLowerCase() === "no" ? "No" : "Yes";
}
function scNormalizeVoteChoice(c) {
  return c === "no" ? "no" : "yes";
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
function scIfVarIsYesNo(varId) {
  return varId === "connected" || varId === "game_window_focused";
}
function scIfLeftValueKind(varId) {
  if (varId === "messageChannel") return "channel";
  if (scIfVarIsYesNo(varId)) return "yesno";
  return "text";
}
function scIfOpsForVariable(varId) {
  if (!varId) return [];
  if (varId === "messageChannel" || scIfVarIsYesNo(varId)) {
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
function scIfIsMulti(ifAction) {
  return !!(ifAction && ifAction.type === "if" && ifAction.conditions && ifAction.conditions.length);
}
function scNormalizeIfConditionObj(obj) {
  if (!obj) return;
  if (!obj.left) {
    obj.op = "contains";
    obj.right = "";
    return;
  }
  var ops = scIfOpsForVariable(obj.left);
  if (!obj.op || !ops.some(function (o) { return o.value === obj.op; })) {
    obj.op = ops.length ? ops[0].value : "contains";
  }
  if (!scIfOpNeedsRight(obj.op)) {
    obj.right = "";
    return;
  }
  if (obj.right == null) obj.right = "";
  if (scIfLeftValueKind(obj.left) === "yesno") {
    if (obj.op !== "is" && obj.op !== "is_not") obj.op = "is";
    obj.right = scNormalizeYesNoValue(obj.right);
    return;
  }
  if (scIfLeftValueKind(obj.left) === "channel" && !obj.right) obj.right = "all";
}
function scNormalizeIfCondition(ifAction) {
  if (!ifAction || ifAction.type !== "if") return;
  if (scIfIsMulti(ifAction)) {
    ifAction.match = ifAction.match === "any" ? "any" : "all";
    ifAction.conditions.forEach(scNormalizeIfConditionObj);
    return;
  }
  scNormalizeIfConditionObj(ifAction);
}
function scIfDefaultCondition() {
  return {left: "", op: "contains", right: ""};
}
function scIfExpandToMulti(ifIdx) {
  if (!scEditing || !scEditing.actions) return;
  var act = scEditing.actions[ifIdx];
  if (!act || act.type !== "if" || scIfIsMulti(act)) return;
  scPushHistory();
  act.match = "all";
  act.conditions = [
    {left: act.left || "", op: act.op || "contains", right: act.right == null ? "" : String(act.right)},
    scIfDefaultCondition()
  ];
  delete act.left;
  delete act.op;
  delete act.right;
  scUpdateIfGroupBlock(ifIdx);
}
function scIfAddCondition(ifIdx) {
  if (!scEditing || !scEditing.actions) return;
  var act = scEditing.actions[ifIdx];
  if (!act || act.type !== "if" || !scIfIsMulti(act)) return;
  scPushHistory();
  act.conditions.push(scIfDefaultCondition());
  scUpdateIfGroupBlock(ifIdx);
}
function scIfRemoveCondition(ifIdx, condIdx) {
  if (!scEditing || !scEditing.actions) return;
  var act = scEditing.actions[ifIdx];
  if (!act || act.type !== "if" || !scIfIsMulti(act)) return;
  if (condIdx < 0 || condIdx >= act.conditions.length) return;
  scPushHistory();
  act.conditions.splice(condIdx, 1);
  if (!act.conditions.length) {
    act.left = "";
    act.op = "contains";
    act.right = "";
    delete act.conditions;
    delete act.match;
  } else if (act.conditions.length === 1) {
    var c = act.conditions[0];
    act.left = c.left || "";
    act.op = c.op || "contains";
    act.right = c.right == null ? "" : String(c.right);
    delete act.conditions;
    delete act.match;
    scNormalizeIfCondition(act);
  }
  scUpdateIfGroupBlock(ifIdx);
}
function scIfConditionLineHtml(cond, ifIdx, condIdx) {
  scNormalizeIfConditionObj(cond);
  var html = scIfLeftFieldHtml(cond, ifIdx, "if-cond", condIdx);
  if (cond.left) {
    html += scPill("op", cond.op, ifIdx, "if-cond", condIdx);
    if (scIfOpNeedsRight(cond.op)) {
      if (scIfLeftValueKind(cond.left) === "channel") {
        html += scPill("right", cond.right || "all", ifIdx, "if-cond", condIdx);
      } else if (scIfLeftValueKind(cond.left) === "yesno") {
        html += scPill("right", cond.right || "Yes", ifIdx, "if-cond", condIdx);
      } else {
        html += scPillInput("right", cond.right, ifIdx, "if-cond", condIdx);
      }
    } else {
      html += scTxt("value");
    }
  }
  html += '<button type="button" class="sc-filter-del" data-remove-if-condition="' + ifIdx + '" data-if-cond-idx="' + condIdx + '" aria-label="Remove condition">\u2212</button>';
  return '<div class="sc-block-filter sc-if-condition" data-if-cond-idx="' + condIdx + '">' + html + '</div>';
}
function scIfMultiBlockHtml(data, idx, kind) {
  scNormalizeIfCondition(data);
  var match = data.match === "any" ? "any" : "all";
  var rows = (data.conditions || []).map(function (c, i) {
    return scIfConditionLineHtml(c, idx, i);
  }).join("");
  return '<div class="sc-if-multi-wrap">' +
    '<div class="sc-if-multi-head sc-block-line">' + scTxt("If") +
    scPill("match", match, idx, kind) + scTxt("are true") + '</div>' +
    '<div class="sc-block-trigger-divider"></div>' +
    '<div class="sc-block-filters sc-if-conditions">' + rows + '</div>' +
    '<button type="button" class="sc-filter-add" data-add-if-condition="' + idx + '">+ Add Condition</button>' +
    '</div>';
}
function scLabelVariable(id, beforeIdx) {
  var vars = scAvailableVariables(beforeIdx == null ? null : beforeIdx);
  for (var i = 0; i < vars.length; i++) {
    if (vars[i].id === id) return vars[i].label;
  }
  if (id === "messageSender" || id === "senderName") return "Message Sender";
  if (id === "messageText" || id === "message") return "Message Text";
  if (id === "messageChannel") return "Message Channel";
  if (id === "messageUClientRoom") return "Message UClient Room";
  if (id === "messageUClientRoomId") return "Message UClient Room ID";
  if (id === "window_active" || id === "foreground_window_title") return "Foreground Window Title";
  if (id === "game_window_focused") return "Game Window Focused";
  if (id === "connected") return "Connected";
  if (id === "server_name") return "Server Name";
  if (id === "map") return "Map";
  if (id === "server_address") return "Server Address";
  if (id === "my_name" || id === "name") return "My Name";
  if (id === "nearestPlayer" || id === "nearest_player") return "Nearest Player";
  if (id === "clipboard") return "Clipboard";
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
      if (a.type === "get") {
        vars.push({id: scGetPropertyVarId(a.property), label: scLabelGetProperty(a.property)});
      }
      if (a.type === "get_clipboard") {
        var clipAs = a.as || "clipboard";
        vars.push({id: clipAs, label: clipAs === "clipboard" ? "Clipboard" : clipAs});
      }
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
  if (a.type === "repeat") {
    a.count = scClampRepeatCount(a.count);
  }
  if (a.type === "run_shortcut") {
    if (a.shortcutId == null) a.shortcutId = "";
    a.shortcutId = String(a.shortcutId || "");
    var runnable = scRunnableManualShortcuts(scEditing && scEditing.id);
    if (a.shortcutId && !runnable.some(function (s) { return s.id === a.shortcutId; })) {
      if (runnable.length) a.shortcutId = runnable[0].id;
      else a.shortcutId = "";
    }
  }
  if (a.type === "if") {
    if (scIfIsMulti(a)) {
      if (!a.match) a.match = "all";
      a.conditions = (a.conditions || []).map(function (c) {
        return {left: c.left == null ? "" : String(c.left), op: c.op || "contains", right: c.right == null ? "" : String(c.right)};
      });
      a.conditions.forEach(function (c, i) {
        if (c.left) {
          var ifVars = scAvailableVariables(actionIdx != null ? actionIdx : 0);
          if (!ifVars.some(function (v) { return v.id === c.left; })) c.left = "";
        }
      });
      scNormalizeIfCondition(a);
    } else {
      if (a.left == null) a.left = "";
      if (a.left) {
        var ifVars2 = scAvailableVariables(actionIdx != null ? actionIdx : 0);
        if (!ifVars2.some(function (v) { return v.id === a.left; })) a.left = "";
      }
      scNormalizeIfCondition(a);
    }
  }
  if (a.type === "get") {
    a.property = scNormalizeGetProperty(a.property);
  }
  if (a.type === "emote") {
    a.emote = scNormalizeEmote(a.emote);
  }
  if (a.type === "vote") {
    a.choice = scNormalizeVoteChoice(a.choice);
  }
  if (a.type === "switch_weapon" || a.type === "switch_weapon_use") {
    a.weapon = a.weapon || "hammer";
  }
  if (a.type === "get_clipboard") {
    if (!a.as) a.as = "clipboard";
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
  if (a.type === "get_clipboard") {
    if (!a.as) a.as = "clipboard";
  }
  if (a.type === "repeat") {
    a.count = scClampRepeatCount(a.count);
  }
  if (a.type === "run_shortcut") {
    a.shortcutId = String(a.shortcutId || "");
    delete a.shortcutName;
  }
  if (a.type === "text") {
    a.parts = scMergeTextParts((a.parts || []).filter(function (p) {
      if (p.mode === "variable") return !!p.variable;
      return p.text != null && String(p.text).length > 0;
    }));
    if (!a.parts.length) a.parts = [{mode: "text", text: ""}];
    if (!a.as) a.as = "text";
  }
  if (a.type === "if") {
    if (scIfIsMulti(a)) {
      a.match = a.match === "any" ? "any" : "all";
      a.conditions = (a.conditions || []).map(function (c) {
        var row = {left: c.left || "", op: c.op || "contains", right: c.right == null ? "" : String(c.right)};
        scNormalizeIfConditionObj(row);
        return row;
      }).filter(function (c) { return c.left; });
      delete a.left;
      delete a.op;
      delete a.right;
      if (!a.conditions.length) {
        delete a.conditions;
        delete a.match;
        a.left = "";
        a.op = "contains";
        a.right = "";
      }
    } else {
      delete a.conditions;
      delete a.match;
      scNormalizeIfConditionObj(a);
    }
  }
  return a;
}
var scSmartBlurTimer = null;
var SC_CHANNEL_CHOICES = [{value: "all", label: "All"}, {value: "team", label: "Team"}, {value: "uclient", label: "UClient"}];
/* Lucide Icons (ISC) — https://lucide.dev — embedded paths from lucide-static */
var SC_LUCIDE_PATHS = {
  "messages-square": '<path d="M14 9a2 2 0 0 1-2 2H6l-4 4V4a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2z"/><path d="M18 9h2a2 2 0 0 1 2 2v11l-4-4h-6a2 2 0 0 1-2-2v-1"/>',
  server: '<rect width="20" height="8" x="2" y="2" rx="2" ry="2"/><rect width="20" height="8" x="2" y="14" rx="2" ry="2"/><line x1="6" x2="6.01" y1="6" y2="6"/><line x1="6" x2="6.01" y1="18" y2="18"/>',
  variable: '<path d="M8 21s-4-3-4-9 4-9 4-9"/><path d="M16 3s4 3 4 9-4 9-4 9"/><line x1="15" x2="9" y1="9" y2="15"/><line x1="9" x2="15" y1="9" y2="15"/>',
  clipboard: '<rect width="8" height="4" x="8" y="2" rx="1" ry="1"/><path d="M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"/>',
  "whole-word": '<circle cx="7" cy="12" r="3"/><path d="M10 9v6"/><circle cx="17" cy="12" r="3"/><path d="M14 7v8"/><path d="M22 17v1c0 .5-.5 1-1 1H3c-.5 0-1-.5-1-1v-1"/>',
  repeat: '<path d="m17 2 4 4-4 4"/><path d="M3 11v-1a4 4 0 0 1 4-4h14"/><path d="m7 22-4-4 4-4"/><path d="M21 13v1a4 4 0 0 1-4 4H3"/>',
  "list-end": '<path d="M16 12H3"/><path d="M16 6H3"/><path d="M10 18H3"/><path d="M21 6v10a2 2 0 0 1-2 2h-5"/><path d="m16 16-2 2 2 2"/>',
  "git-branch": '<line x1="6" x2="6" y1="3" y2="15"/><circle cx="18" cy="6" r="3"/><circle cx="6" cy="18" r="3"/><path d="M18 9a9 9 0 0 1-9 9"/>',
  "between-horizontal-start": '<rect width="13" height="7" x="8" y="3" rx="1"/><path d="m2 9 3 3-3 3"/><rect width="13" height="7" x="8" y="14" rx="1"/>',
  "git-merge": '<circle cx="18" cy="18" r="3"/><circle cx="6" cy="6" r="3"/><path d="M6 21V9a9 9 0 0 0 9 9"/>',
  "circle-stop": '<circle cx="12" cy="12" r="10"/><rect x="9" y="9" width="6" height="6" rx="1"/>',
  plug: '<path d="M12 22v-5"/><path d="M9 8V2"/><path d="M15 8V2"/><path d="M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8Z"/>',
  "log-out": '<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" x2="9" y1="12" y2="12"/>',
  "message-square-text": '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/><path d="M13 8H7"/><path d="M17 12H7"/>',
  timer: '<line x1="10" x2="14" y1="2" y2="2"/><line x1="12" x2="15" y1="14" y2="11"/><circle cx="12" cy="14" r="8"/>',
  hammer: '<path d="m15 12-8.373 8.373a1 1 0 1 1-3-3L12 9"/><path d="m18 15 4-4"/><path d="m21.5 11.5-1.914-1.914A2 2 0 0 1 19 8.172V7l-2.26-2.26a6 6 0 0 0-4.202-1.756L9 2.96l.92.82A6.18 6.18 0 0 1 12 8.4V10l2 2h1.172a2 2 0 0 1 1.414.586L18.5 14.5"/>',
  shirt: '<path d="M20.38 3.46 16 2a4 4 0 0 1-8 0L3.62 3.46a2 2 0 0 0-1.34 2.23l.58 3.47a1 1 0 0 0 .99.84H6v10c0 1.1.9 2 2 2h8a2 2 0 0 0 2-2V10h2.15a1 1 0 0 0 .99-.84l.58-3.47a2 2 0 0 0-1.34-2.23z"/>',
  palette: '<circle cx="13.5" cy="6.5" r=".5" fill="currentColor" stroke="none"/><circle cx="17.5" cy="10.5" r=".5" fill="currentColor" stroke="none"/><circle cx="8.5" cy="7.5" r=".5" fill="currentColor" stroke="none"/><circle cx="6.5" cy="12.5" r=".5" fill="currentColor" stroke="none"/><path d="M12 2C6.5 2 2 6.5 2 12s4.5 10 10 10c.926 0 1.648-.746 1.648-1.688 0-.437-.18-.835-.437-1.125-.29-.289-.438-.652-.438-1.125a1.64 1.64 0 0 1 1.668-1.668h1.996c3.051 0 5.555-2.503 5.555-5.554C21.965 6.012 17.461 2 12 2z"/>',
  "user-round": '<circle cx="12" cy="8" r="5"/><path d="M20 21a8 8 0 0 0-16 0"/>',
  footprints: '<path d="M4 16v-2.38C4 11.5 2.97 10.5 3 8c.03-2.72 1.49-6 4.5-6C9.37 2 10 3.8 10 5.5c0 3.11-2 5.66-2 8.68V16a2 2 0 1 1-4 0Z"/><path d="M20 20v-2.38c0-2.12 1.03-3.12 1-5.62-.03-2.72-1.49-6-4.5-6C14.63 6 14 7.8 14 9.5c0 3.11 2 5.66 2 8.68V20a2 2 0 1 0 4 0Z"/><path d="M16 17h4"/><path d="M4 13h4"/>',
  signature: '<path d="m21 17-2.156-1.868A.5.5 0 0 0 18 15.5v.5a1 1 0 0 1-1 1h-2a1 1 0 0 1-1-1c0-2.545-3.991-3.97-8.5-4a1 1 0 0 0 0 5c4.153 0 4.745-11.295 5.708-13.5a2.5 2.5 0 1 1 3.31 3.284"/><path d="M3 21h18"/>',
  workflow: '<rect width="8" height="8" x="3" y="3" rx="2"/><path d="M7 11v4a2 2 0 0 0 2 2h4"/><rect width="8" height="8" x="13" y="13" rx="2"/>',
  "app-window": '<rect x="2" y="4" width="20" height="16" rx="2"/><path d="M10 4v4"/><path d="M2 8h20"/><path d="M6 4v4"/>',
  play: '<polygon points="6 3 20 12 6 21 6 3" fill="currentColor" stroke="none"/>',
  "chevron-down": '<path d="m6 9 6 6 6-6"/>',
  pencil: '<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z"/><path d="m15 5 4 4"/>',
  copy: '<rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/>',
  "trash-2": '<path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/><line x1="10" x2="10" y1="11" y2="17"/><line x1="14" x2="14" y1="11" y2="17"/>',
  smile: '<circle cx="12" cy="12" r="10"/><path d="M8 14s1.5 2 4 2 4-2 4-2"/><line x1="9" x2="9.01" y1="9" y2="9"/><line x1="15" x2="15.01" y1="9" y2="9"/>',
  skull: '<circle cx="9" cy="12" r="1"/><circle cx="15" cy="12" r="1"/><path d="M8 20v-1a4 4 0 0 1 8 0v1"/><path d="M12 2v2"/><path d="M6.8 4.6 8 6"/><path d="M17.2 4.6 16 6"/><path d="M12 8a4 4 0 0 0-4 4v2h8v-2a4 4 0 0 0-4-4Z"/>',
  "circle-check": '<circle cx="12" cy="12" r="10"/><path d="m9 12 2 2 4-4"/>',
  crosshair: '<circle cx="12" cy="12" r="10"/><line x1="22" x2="18" y1="12" y2="12"/><line x1="6" x2="2" y1="12" y2="12"/><line x1="12" x2="12" y1="6" y2="2"/><line x1="12" x2="12" y1="22" y2="18"/>'
};
var SC_TRIGGER_LUCIDE = {chat_received: "messages-square", server_connect: "server"};
var SC_ACTION_LUCIDE = {
  get: "app-window", get_os_detail: "app-window", get_game_detail: "server", get_clipboard: "clipboard", text: "whole-word", repeat: "repeat", end_repeat: "list-end",
  if: "git-branch", otherwise: "between-horizontal-start", end_if: "git-merge", stop: "circle-stop",
  connect_server: "plug", leave_server: "log-out", send_chat: "message-square-text", wait: "timer",
  switch_weapon_use: "hammer", switch_weapon: "crosshair", emote: "smile", kill: "skull", vote: "circle-check",
  set_skin: "shirt", set_custom_color: "palette", set_body_color: "user-round",
  set_feet_color: "footprints", set_name: "signature", run_shortcut: "play"
};
function scLucideSvg(name, cssClass) {
  var inner = SC_LUCIDE_PATHS[name];
  if (!inner) return "";
  cssClass = cssClass || "sc-block-icon-svg";
  return '<svg class="' + cssClass + '" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + inner + "</svg>";
}
function scInlineLucide(name, cssClass) {
  return scLucideSvg(name, cssClass || "sc-block-icon-svg");
}
function scEdTitleMenuRow(iconName, label, act, destructive) {
  return '<button type="button" class="sc-smart-menu-item sc-ed-title-menu-item' + (destructive ? " is-destructive" : "") +
    '" data-ed-title-act="' + esc(act) + '"><span class="sc-ed-menu-ico">' + scInlineLucide(iconName, "sc-ed-menu-icon-svg") +
    '</span><span class="sc-ed-menu-label">' + esc(label) + "</span></button>";
}
function scEditorDisplayName() {
  if (!scEditing) return "New Shortcut";
  if (scEditing.name && String(scEditing.name).trim()) return String(scEditing.name).trim();
  if (scIsManualEdit()) return "New Shortcut";
  if (scEditing.trigger) return scSummaryTrigger(scEditing.trigger);
  return "New Automation";
}
function scEditorTitleTileHtml() {
  var first = scEditing && scEditing.actions && scEditing.actions[0];
  if (first && first.type) {
    return scBlockIconInner(scActionDef(first.type).tone, scActionDef(first.type).icon, "action", first.type);
  }
  return scInlineLucide("workflow");
}
function scSyncEditorTitleUI() {
  var label = $("sc-ed-title-label");
  var input = $("sc-ed-title");
  var tile = $("sc-ed-title-tile");
  var chev = document.querySelector(".sc-ed-chev-svg");
  if (!scEditing || !label || !input) return;
  var name = scEditorDisplayName();
  input.value = scEditing.name != null ? scEditing.name : "";
  if (!input.value && name) input.placeholder = name;
  label.textContent = name;
  if (tile) tile.innerHTML = scEditorTitleTileHtml();
  if (chev) chev.innerHTML = scInlineLucide("chevron-down", "sc-ed-chev-svg-inner");
}
function scSetEditorTitleMenuOpen(open) {
  var trigger = $("sc-ed-title-trigger");
  if (trigger) trigger.setAttribute("aria-expanded", open ? "true" : "false");
}
function scOpenEditorTitleMenu() {
  if (!scEditing) return;
  var trigger = $("sc-ed-title-trigger");
  if (!trigger) return;
  if (scPopEl && scPopEl.classList.contains("sc-ed-title-pop")) {
    scCloseAllPickers(null);
    return;
  }
  scCloseAllPickers(null);
  scPopEl = document.createElement("div");
  scPopEl.className = "sc-pop sc-ed-title-pop";
  scPopEl.id = "sc-ed-title-menu";
  scPopEl.setAttribute("role", "menu");
  var html = '<div class="sc-smart-menu-inner sc-ed-title-menu">';
  html += scEdTitleMenuRow("pencil", "Rename", "rename");
  html += scEdTitleMenuRow("copy", "Duplicate", "duplicate");
  html += '<div class="sc-smart-menu-sep"></div>';
  html += scEdTitleMenuRow("trash-2", "Delete Shortcut", "delete", true);
  html += "</div>";
  scPopEl.innerHTML = html;
  scPopEl.addEventListener("click", function (e) {
    var row = e.target.closest("[data-ed-title-act]");
    if (!row) return;
    e.stopPropagation();
    scHandleEditorTitleMenu(row.dataset.edTitleAct);
  });
  document.body.appendChild(scPopEl);
  scSetEditorTitleMenuOpen(true);
  var r = trigger.getBoundingClientRect();
  var left = Math.min(Math.max(8, r.left + r.width * 0.5 - scPopEl.offsetWidth * 0.5), window.innerWidth - scPopEl.offsetWidth - 8);
  scPopEl.style.left = left + "px";
  scPopEl.style.top = (r.bottom + 8) + "px";
  scPopEl.style.transformOrigin = Math.max(12, r.left + r.width * 0.5 - left) + "px 0";
}
function scBeginEditorRename() {
  scCloseAllPickers(null);
  var center = $("sc-ed-head-center");
  var input = $("sc-ed-title");
  if (!center || !input) return;
  center.classList.add("is-renaming");
  if (!input.value) input.value = scEditorDisplayName();
  input.focus();
  try { input.setSelectionRange(0, input.value.length); } catch (err) {}
}
function scEndEditorRename() {
  var center = $("sc-ed-head-center");
  var input = $("sc-ed-title");
  if (scEditing && input) scEditing.name = input.value;
  if (center) center.classList.remove("is-renaming");
  scSyncEditorTitleUI();
}
function scDuplicateEditingShortcut() {
  if (!scEditing) return;
  scCloseAllPickers(null);
  scCommitAllPillInputs($("sc-ed-canvas"));
  var copy = JSON.parse(JSON.stringify(scEditing));
  copy.id = scUuid();
  var base = (scEditorDisplayName() || "Shortcut").replace(/\s+Copy(\s+\d+)?$/i, "");
  copy.name = scEntryKind(copy) === "manual"
    ? scUniqueManualShortcutName(base + " Copy", copy.id)
    : base + " Copy";
  shortcutsLocal.push(JSON.parse(JSON.stringify(copy)));
  shortcutsSig = JSON.stringify(shortcutsLocal);
  renderShortcutsGallery();
  renderShortcutsList();
  scAnimateEditorSwap(copy, scEditKind);
  scToast("Duplicated");
}
function scDeleteEditingShortcut() {
  if (!scEditing) return;
  scCloseAllPickers(null);
  send({cmd: "shortcutsDelete", id: scEditing.id});
  shortcutsLocal = shortcutsLocal.filter(function (s) { return s.id !== scEditing.id; });
  shortcutsSig = JSON.stringify(shortcutsLocal);
  renderShortcutsGallery();
  renderShortcutsList();
  scCloseEditor();
}
function scHandleEditorTitleMenu(act) {
  if (act === "rename") scBeginEditorRename();
  else if (act === "duplicate") scDuplicateEditingShortcut();
  else if (act === "delete") scDeleteEditingShortcut();
}
function scBlockIconSvgByType(blockKind, type) {
  if (!type) return "";
  var name = blockKind === "trigger" ? SC_TRIGGER_LUCIDE[type] : SC_ACTION_LUCIDE[type];
  return name ? scLucideSvg(name) : "";
}
function scBlockToneIconSvg(tone) {
  if (tone === "chat") return scLucideSvg("message-square-text");
  if (tone === "text") return scBlockIconSvgByType("action", "text");
  if (tone === "flow") return scBlockIconSvgByType("action", "if");
  if (tone === "clip") return scBlockIconSvgByType("action", "get_clipboard");
  if (tone === "loop") return scBlockIconSvgByType("action", "repeat");
  if (tone === "connect") return scBlockIconSvgByType("trigger", "server_connect");
  if (tone === "wait") return scBlockIconSvgByType("action", "wait");
  if (tone === "stop") return scBlockIconSvgByType("action", "stop");
  return scLucideSvg("workflow");
}
function scBlockIconInner(tone, fallback, blockKind, blockType) {
  var byType = scBlockIconSvgByType(blockKind, blockType);
  if (byType) return byType;
  var byTone = scBlockToneIconSvg(tone);
  if (byTone) return byTone;
  return fallback;
}
function scCatalogBlockIcon(def, kind) {
  var tone = def.tone || (kind === "trigger" ? "chat" : "action");
  var type = (def.defaults && def.defaults.type) || def.id;
  var bk = kind === "trigger" ? "trigger" : "action";
  return scBlockIconInner(tone, def.icon, bk, type);
}
function scVarPillIconSvg(varId) {
  var id = varId || "";
  if (id === "clipboard") return scLucideSvg("clipboard", "sc-pill-icon-svg");
  if (id.indexOf("message") === 0 || id === "senderName" || id === "sender" || id === "message") {
    return scLucideSvg("message-square-text", "sc-pill-icon-svg");
  }
  if (id === "window_active" || id === "foreground_window_title" || id === "game_window_focused") {
    return scLucideSvg("app-window", "sc-pill-icon-svg");
  }
  if (id === "connected" || id === "server_name" || id === "map" || id === "server_address") {
    return scLucideSvg("server", "sc-pill-icon-svg");
  }
  if (id === "text" || id.indexOf("text") === 0) return scLucideSvg("whole-word", "sc-pill-icon-svg");
  return scLucideSvg("variable", "sc-pill-icon-svg");
}
function scVarPillButton(extraClass, varId, label, attrs) {
  return '<button type="button" class="sc-pill var sc-pill-nested' + (extraClass ? " " + extraClass : "") + '"' + (attrs || "") + ">" +
    scVarPillIconSvg(varId) + '<span class="sc-pill-label">' + esc(label) + "</span></button>";
}
function scSmartMenuWouldBeEmpty(beforeIdx, fixedChoices, menuMode, slot) {
  var vars = scSmartFieldVariables(slot, beforeIdx);
  if (menuMode === "variable") return vars.length === 0;
  return vars.length === 0 && !(fixedChoices && fixedChoices.length);
}
function scSmartMenuHtml(beforeIdx, fixedChoices, menuMode, slot) {
  if (scSmartMenuWouldBeEmpty(beforeIdx, fixedChoices, menuMode, slot)) return "";
  var vars = scSmartFieldVariables(slot, beforeIdx);
  var html = '<div class="sc-smart-menu" aria-hidden="true"><div class="sc-smart-menu-inner">';
  var showVars = vars.length > 0;
  if (menuMode === "variable" && showVars) {
    html += '<button type="button" class="sc-smart-menu-item sc-smart-menu-clear" data-pick-type="clear" data-pick-id="">Clear Variable</button>';
    html += '<div class="sc-smart-menu-sep"></div>';
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
  scHideTextVarPop();
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
  if (!menu || !menu.querySelector(".sc-smart-menu-item")) return;
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
function scHandleSmartMenuPick(menuItem) {
  if (!menuItem) return;
  var pickMenu = menuItem.closest(".sc-smart-menu");
  var field = (pickMenu && pickMenu._scPortalField) || menuItem.closest(".sc-smart-field");
  if (!field) return;
  scApplySmartPick(field, menuItem.dataset.pickType, menuItem.dataset.pickId);
}
function scApplySmartPick(fieldEl, pickType, pickId) {
  if (!fieldEl) return;
  var slot = fieldEl.dataset.smartSlot;
  var idx = Number(fieldEl.dataset.idx || 0);
  var kind = fieldEl.dataset.kind;
  var condIdx = fieldEl.dataset.condIdx != null ? Number(fieldEl.dataset.condIdx) : null;
  var partIdx = fieldEl.dataset.partIdx != null ? Number(fieldEl.dataset.partIdx) : null;
  var ref = scGetBlockRef(kind, idx, condIdx);
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
    }     else if (slot === "ifLeft") {
      ref.left = pickId || "";
      scNormalizeIfConditionObj(ref);
    }
  }
  scSmartFieldHideMenu(fieldEl);
  if (kind === "if-cond") scUpdateIfGroupBlock(idx);
  else scUpdateBlockElement(kind, idx);
}
function scIfLeftFieldHtml(data, idx, kind, condIdx) {
  var left = data.left || "";
  var condAttr = condIdx != null ? ' data-cond-idx="' + condIdx + '"' : "";
  var html = '<span class="sc-smart-field" data-smart-slot="ifLeft" data-idx="' + idx + '" data-kind="' + esc(kind) + '"' + condAttr + '">';
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
function scMergeTextParts(parts, trackFocusIdx) {
  if (!parts || !parts.length) {
    return trackFocusIdx != null ? {parts: [{mode: "text", text: ""}], focusIdx: 0} : [{mode: "text", text: ""}];
  }
  var out = [];
  var tracked = trackFocusIdx;
  parts.forEach(function (p, i) {
    if (p.mode === "variable") {
      out.push({mode: "variable", variable: p.variable || "messageSender"});
      if (i === trackFocusIdx) tracked = out.length - 1;
      return;
    }
    var text = p.text == null ? "" : String(p.text);
    if (out.length && out[out.length - 1].mode === "text") {
      if (i === trackFocusIdx) tracked = out.length - 1;
      out[out.length - 1].text += text;
    } else {
      out.push({mode: "text", text: text});
      if (i === trackFocusIdx) tracked = out.length - 1;
    }
  });
  if (!out.length) out.push({mode: "text", text: ""});
  if (out[out.length - 1].mode === "variable") out.push({mode: "text", text: ""});
  if (trackFocusIdx != null) {
    if (tracked == null || tracked >= out.length) tracked = out.length - 1;
    while (tracked >= 0 && out[tracked].mode !== "text") tracked--;
    if (tracked < 0) tracked = 0;
    return {parts: out, focusIdx: tracked};
  }
  return out;
}
var scTextVarPopState = {anchor: null, el: null, blockIdx: null, kind: null, partIdx: null};
function scHideTextVarPop() {
  var pop = $("sc-text-var-pop");
  if (pop) {
    pop.style.display = "none";
    pop.setAttribute("aria-hidden", "true");
    pop.innerHTML = "";
  }
  (document.querySelectorAll(".sc-text-var-bar") || []).forEach(function (bar) {
    var composer = bar.closest(".sc-text-composer");
    if (composer) composer.classList.remove("sc-text-var-open");
    bar.remove();
  });
  scTextVarPopState = {anchor: null, el: null, blockIdx: null, kind: null, partIdx: null};
}
function scTextVarBarHtml(blockIdx, kind) {
  var vars = scSmartFieldVariables("textPart", blockIdx);
  return vars.map(function (v) {
    return '<button type="button" class="sc-text-var-chip" data-text-var="' + esc(v.id) + '" data-idx="' + blockIdx + '" data-kind="' + esc(kind || "action") + '">' +
      scVarPillIconSvg(v.id) + "<span>" + esc(v.label) + "</span></button>";
  }).join("");
}
function scShowTextVarPopForBlock(blockIdx, kind) {
  var vars = scSmartFieldVariables("textPart", blockIdx);
  if (!vars.length) {
    scHideTextVarPop();
    return;
  }
  var composer = scTextVarPopState.anchor;
  if (!composer) {
    var block = document.querySelector('.sc-block[data-block-kind="action"][data-block-idx="' + blockIdx + '"]');
    composer = block ? block.querySelector(".sc-text-composer") : null;
  }
  if (!composer) return;
  var pop = $("sc-text-var-pop");
  if (pop) {
    pop.style.display = "none";
    pop.innerHTML = "";
    pop.setAttribute("aria-hidden", "true");
  }
  var bar = composer.querySelector(".sc-text-var-bar");
  if (!bar) {
    bar = document.createElement("div");
    bar.className = "sc-text-var-bar";
    bar.setAttribute("role", "toolbar");
    bar.setAttribute("aria-label", "Insert variable");
    composer.insertBefore(bar, composer.firstChild);
  }
  bar.innerHTML = scTextVarBarHtml(blockIdx, kind);
  composer.classList.add("sc-text-var-open");
}
function scTextVarChipInlineHtml(varId, pi, idx) {
  return '<span class="sc-text-var-inline" data-text-part="variable" data-var-id="' + esc(varId) + '" data-name-idx="' + pi + '" contenteditable="false">' +
    scVarPillIconSvg(varId) + '<span class="sc-pill-label">' + esc(scLabelVariable(varId, idx)) + "</span></span>";
}
function scTextSegmentHtml(text, pi, idx, kind, showPlaceholder) {
  var ph = showPlaceholder ? ' placeholder="Text"' : "";
  return '<textarea class="sc-text-segment-input" data-text-part="text" data-input="textPart" data-name-idx="' + pi + '" data-idx="' + idx + '" data-kind="' + kind + '" rows="1" spellcheck="false"' + ph + ">" +
    esc(text == null ? "" : String(text)) + "</textarea>";
}
function scTextComposerInnerHtml(parts, idx, kind) {
  parts = scMergeTextParts(parts || []);
  var onlyText = parts.length === 1 && parts[0].mode === "text";
  var html = "";
  parts.forEach(function (p, pi) {
    if (p.mode === "variable") html += scTextVarChipInlineHtml(p.variable || "messageSender", pi, idx);
    else html += scTextSegmentHtml(p.text, pi, idx, kind, onlyText && pi === 0);
  });
  return html;
}
function scTextComposerWrap(idx, kind, inner) {
  return '<div class="sc-text-composer-wrap" data-text-block="' + idx + '">' + inner + "</div>";
}
function scTextComposerHtml(parts, idx, kind) {
  var inner = '<div class="sc-text-composer" data-idx="' + idx + '" data-kind="' + kind + '" tabindex="-1">' +
    scTextComposerInnerHtml(parts, idx, kind) + "</div>";
  return scTextComposerWrap(idx, kind, inner);
}
function scRememberTextCaret(ta) {
  if (!ta || ta.tagName !== "TEXTAREA") return;
  var len = (ta.value || "").length;
  var start = ta.selectionStart == null ? len : ta.selectionStart;
  var end = ta.selectionEnd == null ? start : ta.selectionEnd;
  ta._scCaret = {start: start, end: end};
}
function scTextInputCaret(input) {
  if (!input) return {start: 0, end: 0};
  var text = input.value || "";
  var len = text.length;
  if (input.dataset.scComposing === "1") {
    if (input._scCaret) return input._scCaret;
    return {start: len, end: len};
  }
  var start = input.selectionStart == null ? len : input.selectionStart;
  var end = input.selectionEnd == null ? start : input.selectionEnd;
  if (start < 0 || start > len) start = len;
  if (end < 0 || end > len) end = start;
  return {start: start, end: end};
}
function scFocusTextSegmentAfterVar(blockIdx, focusPartIdx, caretPos) {
  requestAnimationFrame(function () {
    var block = document.querySelector('.sc-block[data-block-kind="action"][data-block-idx="' + blockIdx + '"]');
    if (!block) return;
    var ta = block.querySelector('textarea.sc-text-segment-input[data-name-idx="' + focusPartIdx + '"]');
    if (!ta) {
      var all = block.querySelectorAll("textarea.sc-text-segment-input");
      ta = all.length ? all[all.length - 1] : null;
    }
    if (!ta) return;
    scFitTextSegment(ta);
    ta.focus();
    var pos = caretPos == null ? 0 : caretPos;
    var max = (ta.value || "").length;
    if (pos < 0) pos = 0;
    if (pos > max) pos = max;
    try { ta.setSelectionRange(pos, pos); } catch (err) {}
    scRememberTextCaret(ta);
    var composer = ta.closest(".sc-text-composer");
    scTextVarPopState = {
      anchor: composer, el: ta, blockIdx: String(blockIdx), kind: ta.dataset.kind || "action",
      partIdx: ta.dataset.nameIdx
    };
    scShowTextVarPopForBlock(blockIdx, ta.dataset.kind);
  });
}
function scInsertTextVariableAtCursor(varId) {
  if (!varId || !scEditing) return;
  var state = scTextVarPopState;
  var idx = Number(state.blockIdx != null ? state.blockIdx : NaN);
  var kind = state.kind || "action";
  var ref = scGetBlockRef(kind, idx);
  if (!ref || ref.type !== "text") return;
  scCaptureTextPartsFromDom(idx, ref);
  var pi = Number(state.partIdx);
  var input = state.el;
  if (!input || isNaN(pi) || !ref.parts[pi] || ref.parts[pi].mode !== "text") {
    ref.parts = scMergeTextParts(ref.parts.concat([{mode: "variable", variable: varId}, {mode: "text", text: ""}]));
    scUpdateBlockElement("action", idx);
    scFocusTextSegmentAfterVar(idx, ref.parts.length - 1, 0);
    return;
  }
  if (document.activeElement === input) scRememberTextCaret(input);
  var text = input.value || "";
  var caret = scTextInputCaret(input);
  var start = caret.start;
  var end = caret.end;
  var before = text.slice(0, start);
  var after = text.slice(end);
  var rebuilt = ref.parts.slice(0, pi);
  if (before) rebuilt.push({mode: "text", text: before});
  rebuilt.push({mode: "variable", variable: varId});
  var focusPi = rebuilt.length;
  rebuilt.push({mode: "text", text: after});
  rebuilt = rebuilt.concat(ref.parts.slice(pi + 1));
  var merged = scMergeTextParts(rebuilt, focusPi);
  ref.parts = merged.parts;
  scUpdateBlockElement("action", idx);
  scFocusTextSegmentAfterVar(idx, merged.focusIdx, 0);
}
function scReplaceTextVariableId(blockIdx, fromId, toId, scopeShortcut, skipRender) {
  if (!scEditing || !fromId || !toId || fromId === toId) return false;
  var actions = scEditing.actions;
  if (!actions) return false;
  var touched = false;
  for (var i = 0; i < actions.length; i++) {
    if (scopeShortcut !== true && i !== blockIdx) continue;
    var a = actions[i];
    if (!a || a.type !== "text" || !a.parts) continue;
    var changed = false;
    a.parts.forEach(function (p) {
      if (p.mode === "variable" && p.variable === fromId) {
        p.variable = toId;
        changed = true;
      }
    });
    if (changed) {
      a.parts = scMergeTextParts(a.parts);
      touched = true;
      if (!scopeShortcut) {
        scUpdateBlockElement("action", blockIdx);
        return true;
      }
    }
  }
  if (touched && scopeShortcut && !skipRender) scRenderEditor();
  return touched;
}
function scReplaceIfVariableId(fromId, toId) {
  if (!scEditing || !fromId || !toId || fromId === toId) return false;
  var actions = scEditing.actions;
  if (!actions) return false;
  var touched = false;
  for (var i = 0; i < actions.length; i++) {
    var a = actions[i];
    if (!a || a.type !== "if") continue;
    if (scIfIsMulti(a)) {
      (a.conditions || []).forEach(function (c) {
        if (c && c.left === fromId) {
          c.left = toId;
          scNormalizeIfConditionObj(c);
          touched = true;
        }
      });
    } else if (a.left === fromId) {
      a.left = toId;
      scNormalizeIfConditionObj(a);
      touched = true;
    }
  }
  return touched;
}
function scRemoveTextVariableId(blockIdx, varId, scopeShortcut) {
  if (!scEditing || !varId) return;
  var actions = scEditing.actions;
  if (!actions) return;
  var touched = false;
  for (var i = 0; i < actions.length; i++) {
    if (scopeShortcut !== true && i !== blockIdx) continue;
    var a = actions[i];
    if (!a || a.type !== "text" || !a.parts) continue;
    var filtered = a.parts.filter(function (p) {
      return !(p.mode === "variable" && p.variable === varId);
    });
    if (filtered.length === a.parts.length) continue;
    a.parts = scMergeTextParts(filtered.length ? filtered : [{mode: "text", text: ""}]);
    touched = true;
    if (!scopeShortcut) {
      scUpdateBlockElement("action", blockIdx);
      scFocusTextSegmentAfterVar(blockIdx, 0, 0);
      return;
    }
  }
  if (touched && scopeShortcut) scRenderEditor();
}
function scRemapVariableIdInShortcut(fromId, toId) {
  var touched = scReplaceTextVariableId(0, fromId, toId, true, true);
  if (scReplaceIfVariableId(fromId, toId)) touched = true;
  if (touched) scRenderEditor();
}
function scAllowedVariableIdMap(beforeIdx) {
  var map = Object.create(null);
  scAvailableVariables(beforeIdx).forEach(function (v) {
    map[v.id] = true;
  });
  return map;
}
function scPruneStaleTextVariables() {
  if (!scEditing || !scEditing.actions) return false;
  var touched = false;
  scEditing.actions.forEach(function (a, i) {
    if (!a || a.type !== "text" || !a.parts) return;
    scNormalizeAction(a, i);
    var allowed = scAllowedVariableIdMap(i);
    var filtered = a.parts.filter(function (p) {
      if (p.mode !== "variable") return true;
      if (allowed[p.variable]) return true;
      touched = true;
      return false;
    });
    if (filtered.length !== a.parts.length) {
      a.parts = scMergeTextParts(filtered.length ? filtered : [{mode: "text", text: ""}]);
    }
  });
  return touched;
}
function scTextBackspaceRemoveVar(input) {
  if (!input || input.value || input.selectionStart !== 0 || input.selectionEnd !== 0) return false;
  var pi = Number(input.dataset.nameIdx);
  if (isNaN(pi) || pi <= 0) return false;
  var idx = Number(input.dataset.idx || 0);
  var kind = input.dataset.kind || "action";
  var ref = scGetBlockRef(kind, idx);
  if (!ref || ref.type !== "text") return false;
  scCaptureTextPartsFromDom(idx, ref);
  if (pi >= ref.parts.length || ref.parts[pi - 1].mode !== "variable") return false;
  scPushHistory();
  var curText = ref.parts[pi].text || "";
  var rebuilt = ref.parts.slice(0, pi - 1);
  var rest = ref.parts.slice(pi + 1);
  var caretPos = 0;
  if (rebuilt.length && rebuilt[rebuilt.length - 1].mode === "text") {
    caretPos = (rebuilt[rebuilt.length - 1].text || "").length;
    rebuilt[rebuilt.length - 1] = {mode: "text", text: (rebuilt[rebuilt.length - 1].text || "") + curText};
  } else {
    rebuilt.push({mode: "text", text: curText || ""});
  }
  var focusPiBefore = rebuilt.length - 1;
  rebuilt = rebuilt.concat(rest);
  ref.parts = scMergeTextParts(rebuilt);
  var focusPi = Math.min(focusPiBefore, ref.parts.length - 1);
  while (focusPi >= 0 && ref.parts[focusPi].mode !== "text") focusPi--;
  if (focusPi < 0) {
    for (var k = 0; k < ref.parts.length; k++) {
      if (ref.parts[k].mode === "text") { focusPi = k; break; }
    }
  }
  scUpdateBlockElement("action", idx);
  scFocusTextSegmentAfterVar(idx, focusPi, caretPos);
  return true;
}
function scFocusTextComposerFromEvent(composer, clientX) {
  if (!composer) return;
  var tas = composer.querySelectorAll("textarea.sc-text-segment-input");
  if (!tas.length) return;
  var target = tas[tas.length - 1];
  if (clientX != null) {
    for (var i = 0; i < tas.length; i++) {
      var r = tas[i].getBoundingClientRect();
      if (clientX >= r.left - 4 && clientX <= r.right + 4) {
        target = tas[i];
        break;
      }
    }
  }
  scFitTextSegment(target);
  target.focus();
  var len = (target.value || "").length;
  try { target.setSelectionRange(len, len); } catch (err) {}
  scRememberTextCaret(target);
  var blockIdx = Number(target.dataset.idx || 0);
  scTextVarPopState = {
    anchor: composer, el: target, blockIdx: String(blockIdx), kind: target.dataset.kind || "action",
    partIdx: target.dataset.nameIdx
  };
  scShowTextVarPopForBlock(blockIdx, target.dataset.kind);
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
  var bodyHtml = scTextComposerHtml(data.parts, idx, kind);
  return '<div class="sc-text-head"><span class="sc-text-title">Text</span></div>' + bodyHtml;
}
function scCaptureTextPartsFromDom(idx, ref) {
  if (!ref || ref.type !== "text") return;
  scNormalizeAction(ref, idx);
  var block = document.querySelector('.sc-block[data-block-kind="action"][data-block-idx="' + idx + '"]');
  if (!block) return;
  var composer = block.querySelector(".sc-text-composer");
  if (!composer) return;
  var parts = [];
  composer.querySelectorAll("[data-text-part]").forEach(function (el) {
    if (el.dataset.textPart === "variable") {
      parts.push({mode: "variable", variable: el.dataset.varId || ""});
    } else if (el.dataset.textPart === "text") {
      parts.push({mode: "text", text: el.value});
    }
  });
  ref.parts = scMergeTextParts(parts.length ? parts : [{mode: "text", text: ""}]);
}
function scAppendTextVariable(idx, kind, varId) {
  if (!varId || !scEditing) return;
  if (!scTextVarPopState.el) {
    var block = document.querySelector('.sc-block[data-block-kind="action"][data-block-idx="' + idx + '"]');
    var ta = block ? block.querySelector("textarea.sc-text-segment-input") : null;
    var composer = block ? block.querySelector(".sc-text-composer") : null;
    if (ta && composer) {
      scTextVarPopState = {
        anchor: composer, el: ta, blockIdx: String(idx), kind: kind || "action", partIdx: ta.dataset.nameIdx
      };
    }
  }
  if (scTextVarPopState.el && document.activeElement === scTextVarPopState.el) {
    scRememberTextCaret(scTextVarPopState.el);
  }
  scPushHistory();
  scInsertTextVariableAtCursor(varId);
}
function setRailView(view) {
  activeRailView = view;
  $("shell").classList.toggle("shortcuts-mode", view === "shortcuts");
  $("btn-home").classList.toggle("on", view === "home");
  $("btn-shortcuts").classList.toggle("on", view === "shortcuts");
  $("shortcuts-view").setAttribute("aria-hidden", view === "shortcuts" ? "false" : "true");
  if (view === "shortcuts") requestAnimationFrame(scLayoutLibTabIndicator);
  if (settingsOpen) toggleSettings(false);
}
$("btn-home").addEventListener("click", function () { setRailView("home"); });
$("btn-shortcuts").addEventListener("click", function () { setRailView("shortcuts"); });

function scEntryKind(sc) {
  if (sc && sc.kind === "manual") return "manual";
  if (sc && sc.kind === "automation") return "automation";
  return sc && sc.trigger ? "automation" : "manual";
}
function scManualShortcuts() {
  return shortcutsLocal.filter(function (sc) { return scEntryKind(sc) === "manual"; });
}
function scRunnableManualShortcuts(excludeId) {
  return shortcutsLocal.filter(function (sc) {
    if (scEntryKind(sc) !== "manual") return false;
    if (!sc.actions || !sc.actions.length) return false;
    if (excludeId && sc.id === excludeId) return false;
    return true;
  });
}
function scRunShortcutLabel(shortcutId) {
  if (!shortcutId) return "Choose Shortcut";
  var sc = shortcutsLocal.find(function (s) { return s.id === shortcutId; });
  if (!sc) return "Missing Shortcut";
  return (sc.name || "Shortcut").trim() || "Shortcut";
}
function scAutomationShortcuts() {
  return shortcutsLocal.filter(function (sc) { return scEntryKind(sc) === "automation"; });
}
function scTileTone(id) {
  var hash = 0;
  for (var i = 0; i < id.length; i++) hash = ((hash << 5) - hash + id.charCodeAt(i)) | 0;
  return SC_TILE_TONES[Math.abs(hash) % SC_TILE_TONES.length];
}
function scTileIcon(sc) {
  var first = sc.actions && sc.actions[0];
  if (first && first.type) return scActionDef(first.type).icon;
  return "\u2699";
}
function scRunShortcutActions(sc) {
  if (!sc || !sc.actions || !sc.actions.length) { scToast("Add at least one action"); return; }
  if (!lastState || !lastState.gameRunning) { scToast("Start the game first"); return; }
  var actions = sc.actions.map(scCleanActionForSave);
  send({cmd: "automationRun", runId: scUuid(), actions: actions});
}
function scLayoutLibTabIndicator() {
  var tabs = document.querySelector(".sc-lib-tabs");
  if (!tabs) return;
  var indicator = tabs.querySelector(".sc-lib-tabs-indicator");
  var active = tabs.querySelector(".sc-lib-tab.on");
  if (!indicator || !active) return;
  indicator.style.width = active.offsetWidth + "px";
  indicator.style.transform = "translateX(" + active.offsetLeft + "px)";
}
function setScLibraryTab(tab) {
  var next = tab === "automation" ? "automation" : "shortcuts";
  if (next === scLibraryTab && $("sc-panel-shortcuts").classList.contains("on") === (next === "shortcuts")) {
    scLayoutLibTabIndicator();
    return;
  }
  scLibraryTab = next;
  $("shortcuts-view").setAttribute("data-sc-lib", scLibraryTab);
  document.querySelectorAll(".sc-lib-tab").forEach(function (btn) {
    var on = btn.dataset.scLib === scLibraryTab;
    btn.classList.toggle("on", on);
    btn.setAttribute("aria-selected", on ? "true" : "false");
  });
  $("sc-panel-shortcuts").classList.toggle("on", scLibraryTab === "shortcuts");
  $("sc-panel-automation").classList.toggle("on", scLibraryTab === "automation");
  $("shortcuts-view").classList.toggle("empty-shortcuts", scLibraryTab === "shortcuts" && !scManualShortcuts().length);
  $("shortcuts-view").classList.toggle("empty-automation", scLibraryTab === "automation" && !scAutomationShortcuts().length);
  requestAnimationFrame(scLayoutLibTabIndicator);
}
function renderShortcutsGallery() {
  var grid = $("sc-gallery-grid");
  var q = (scGallerySearch || "").trim().toLowerCase();
  var items = scManualShortcuts().filter(function (sc) {
    if (!q) return true;
    return String(sc.name || "").toLowerCase().indexOf(q) >= 0;
  });
  $("shortcuts-view").classList.toggle("empty-shortcuts", !scManualShortcuts().length);
  if (!items.length) {
    grid.innerHTML = '<div class="sc-empty" style="grid-column:1/-1"><b>' +
      (q ? "No matches" : "No shortcuts yet") + '</b>' +
      (q ? "Try another search." : "Tap + to build a shortcut with Do actions only.") + '</div>';
    return;
  }
  grid.innerHTML = items.map(function (sc) {
    return '<div class="sc-tile ' + scTileTone(sc.id) +
      '" role="button" tabindex="0" data-id="' + esc(sc.id) + '">' +
      '<span class="sc-tile-top">' +
      '<span class="sc-tile-ico">' + esc(scTileIcon(sc)) + '</span>' +
      '<button type="button" class="sc-tile-play" data-play="' + esc(sc.id) + '" title="Run shortcut" aria-label="Run shortcut">' +
      SC_PLAY_ICON + '</button></span>' +
      '<span class="sc-tile-name">' + esc(sc.name || "Shortcut") + '</span></div>';
  }).join("");
}
function renderShortcutsList() {
  var list = $("sc-list");
  var automations = scAutomationShortcuts();
  $("shortcuts-view").classList.toggle("empty-automation", !automations.length);
  if (!automations.length) {
    list.innerHTML = '<div class="sc-empty"><b>No automations yet</b>Create a When trigger to detect chat, then chain actions.</div>';
    return;
  }
  list.innerHTML = automations.map(function (sc) {
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
  var incoming = JSON.stringify(st.shortcuts || []);
  if (incoming === shortcutsSig) return;
  shortcutsLocal = JSON.parse(incoming);
  var beforeDedupe = JSON.stringify(shortcutsLocal);
  scEnsureUniqueManualShortcutNames();
  shortcutsSig = JSON.stringify(shortcutsLocal);
  if (shortcutsSig !== beforeDedupe) {
    send({cmd: "shortcutsSave", shortcuts: shortcutsLocal});
  }
  renderShortcutsGallery();
  renderShortcutsList();
  setScLibraryTab(scLibraryTab);
}
function scToast(msg) {
  var t = $("sc-toast");
  t.textContent = msg || "Saved";
  t.classList.add("on");
  clearTimeout(scToast._timer);
  scToast._timer = setTimeout(function () { t.classList.remove("on"); }, 1600);
}
function scShortcutNameKey(name) {
  return String(name || "").trim().toLowerCase();
}
function scManualNameConflicts(name, selfId) {
  var key = scShortcutNameKey(name);
  if (!key) return false;
  return shortcutsLocal.some(function (s) {
    return scEntryKind(s) === "manual" && s.id !== selfId && scShortcutNameKey(s.name) === key;
  });
}
function scUniqueManualShortcutName(desired, selfId) {
  var base = String(desired || "").trim() || "New Shortcut";
  if (!scManualNameConflicts(base, selfId)) return base;
  for (var i = 1; i < 1000; i++) {
    var cand = base + " (" + i + ")";
    if (!scManualNameConflicts(cand, selfId)) return cand;
  }
  return base + " (" + Date.now() + ")";
}
function scEnsureUniqueManualShortcutNames() {
  shortcutsLocal.forEach(function (sc) {
    if (scEntryKind(sc) !== "manual") return;
    sc.name = scUniqueManualShortcutName(sc.name, sc.id);
  });
}
function scSaveAll() {
  scEnsureUniqueManualShortcutNames();
  send({cmd: "shortcutsSave", shortcuts: shortcutsLocal});
  scToast("Saved");
}
function scClosePop() {
  scClearStepperHold();
  scSetEditorTitleMenuOpen(false);
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
document.addEventListener("mousedown", function (e) {
  var menuItem = e.target.closest(".sc-smart-menu-item");
  if (!menuItem) return;
  e.preventDefault();
  scHandleSmartMenuPick(menuItem);
}, true);
document.addEventListener("click", function (e) {
  if (e.target.closest(".sc-smart-menu-item")) return;
  if (e.target.closest(".sc-pop") || e.target.closest(".sc-smart-menu")) return;
  if (e.target.closest(".sc-smart-trigger")) return;
  if (e.target.closest(".sc-pill[data-field]")) return;
  if (e.target.closest(".sc-stepper-pill") || e.target.closest(".sc-stepper-pop")) return;
  if (e.target.closest("#sc-ed-title-trigger") || e.target.closest(".sc-ed-title-pop")) return;
  if (e.target.closest(".sc-text-composer") || e.target.closest(".sc-text-var-bar") ||
      e.target.closest("#sc-text-var-pop")) return;
  scCloseAllPickers(null);
});

function scClampRepeatCount(raw) {
  var n = parseInt(String(raw == null ? "" : raw).replace(/[^0-9]/g, ""), 10);
  if (!isFinite(n) || n < 1) n = 1;
  if (n > 99) n = 99;
  return n;
}
function scRepeatTimesLabel(count) {
  var n = scClampRepeatCount(count);
  return n + (n === 1 ? " time" : " times");
}
function scRepeatTimesControl(count, idx, kind) {
  return '<button type="button" class="sc-pill sc-stepper-pill sc-repeat-times-pill" data-stepper="repeat" data-repeat-times="' + idx + '" data-kind="' + esc(kind) + '">' +
    esc(scRepeatTimesLabel(count)) + '</button>';
}
function scWaitSecondsLabel(seconds) {
  var n = scClampWaitSeconds(seconds);
  return n + (n === 1 ? " second" : " seconds");
}
function scWaitSecondsControl(seconds, idx, kind) {
  return '<button type="button" class="sc-pill sc-stepper-pill sc-wait-seconds-pill" data-stepper="wait" data-wait-seconds="' + idx + '" data-kind="' + esc(kind) + '">' +
    esc(scWaitSecondsLabel(seconds)) + '</button>';
}
function scUpdateStepperPopAnchor(mode, ref) {
  if (!scPopEl || !scPopEl._scStepperAnchor || !ref) return;
  if (mode === "repeat") scPopEl._scStepperAnchor.textContent = scRepeatTimesLabel(ref.count);
  else if (mode === "wait") scPopEl._scStepperAnchor.textContent = scWaitSecondsLabel(ref.seconds);
}
var scStepperHoldTimer = null;
var scStepperHoldState = null;
function scClearStepperHold() {
  if (scStepperHoldTimer) {
    clearTimeout(scStepperHoldTimer);
    scStepperHoldTimer = null;
  }
  scStepperHoldState = null;
}
function scAdjustRepeatCount(idx, kind, delta, skipHistory) {
  if (!scEditing || !scEditing.actions) return;
  var ref = scGetBlockRef(kind, idx);
  if (!ref || ref.type !== "repeat") return;
  if (!skipHistory) scPushHistory();
  ref.count = scClampRepeatCount(scClampRepeatCount(ref.count) + delta);
  scUpdateRepeatGroupBlock(idx);
  scUpdateStepperPopAnchor("repeat", ref);
}
function scAdjustWaitSeconds(idx, kind, delta, skipHistory) {
  if (!scEditing || !scEditing.actions) return;
  var ref = scGetBlockRef(kind, idx);
  if (!ref || ref.type !== "wait") return;
  if (!skipHistory) scPushHistory();
  ref.seconds = scClampWaitSeconds(scClampWaitSeconds(ref.seconds) + delta);
  scUpdateBlockElement(kind, idx);
  scUpdateStepperPopAnchor("wait", ref);
}
function scAdjustStepperByMode(idx, kind, mode, delta, skipHistory) {
  if (mode === "repeat") scAdjustRepeatCount(idx, kind, delta, skipHistory);
  else if (mode === "wait") scAdjustWaitSeconds(idx, kind, delta, skipHistory);
}
function scOpenStepperPop(anchor, idx, kind, mode) {
  scCloseAllPickers(null);
  scPopEl = document.createElement("div");
  scPopEl.className = "sc-pop sc-stepper-pop";
  scPopEl.innerHTML = '<div class="sc-smart-menu-inner">' +
    '<button type="button" class="sc-stepper-btn" data-step-delta="-1" aria-label="Decrease">−</button>' +
    '<span class="sc-stepper-sep" aria-hidden="true"></span>' +
    '<button type="button" class="sc-stepper-btn" data-step-delta="1" aria-label="Increase">+</button></div>';
  scPopEl._scStepperAnchor = anchor;
  scPopEl._scStepperMode = mode;
  document.body.appendChild(scPopEl);
  var r = anchor.getBoundingClientRect();
  var left = Math.min(r.left, window.innerWidth - scPopEl.offsetWidth - 8);
  scPopEl.style.left = left + "px";
  scPopEl.style.top = (r.bottom + 6) + "px";
  scPopEl.style.transformOrigin = Math.max(12, r.left + r.width * 0.5 - left) + "px 0";
  function onStep(btn) {
    var delta = Number(btn.dataset.stepDelta || 0);
    if (!delta) return;
    scBeginStepperHold(idx, kind, mode, delta);
  }
  scPopEl.querySelectorAll(".sc-stepper-btn").forEach(function (btn) {
    btn.addEventListener("pointerdown", function (e) {
      if (e.button !== 0) return;
      e.preventDefault();
      e.stopPropagation();
      onStep(btn);
    });
  });
}
function scOpenRepeatStepperPop(anchor, idx, kind) {
  scOpenStepperPop(anchor, idx, kind, "repeat");
}
function scOpenWaitStepperPop(anchor, idx, kind) {
  scOpenStepperPop(anchor, idx, kind, "wait");
}
function scBeginStepperHold(idx, kind, mode, delta) {
  scClearStepperHold();
  scAdjustStepperByMode(idx, kind, mode, delta, false);
  var start = performance.now();
  scStepperHoldState = {idx: idx, kind: kind, mode: mode, delta: delta, start: start};
  function tick() {
    if (!scStepperHoldState || scStepperHoldState.idx !== idx || scStepperHoldState.mode !== mode) return;
    var elapsed = performance.now() - scStepperHoldState.start;
    if (elapsed >= 400) scAdjustStepperByMode(idx, kind, mode, delta, true);
    var delay = elapsed >= 1200 ? 50 : (elapsed >= 400 ? 110 : 350);
    scStepperHoldTimer = setTimeout(tick, delay);
  }
  scStepperHoldTimer = setTimeout(tick, 400);
}
function scFindMatchingEndRepeat(actions, repeatIdx) {
  if (!actions || repeatIdx < 0 || repeatIdx >= actions.length || actions[repeatIdx].type !== "repeat") return -1;
  var depth = 1;
  for (var i = repeatIdx + 1; i < actions.length; i++) {
    if (actions[i].type === "repeat") depth++;
    else if (actions[i].type === "end_repeat") {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}
function scRepeatBlockMeta(actions, repeatIdx, endIdx, idx) {
  if (idx === repeatIdx) return {role: "repeat"};
  if (idx === endIdx) return {role: "end"};
  return {role: "branch"};
}
function scIsInsideRepeatGroup(idx) {
  var actions = scEditing && scEditing.actions;
  if (!actions) return false;
  for (var i = 0; i < actions.length; i++) {
    if (actions[i].type !== "repeat") continue;
    var endIdx = scFindMatchingEndRepeat(actions, i);
    if (endIdx >= 0 && idx > i && idx <= endIdx) return true;
  }
  return false;
}
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
function scRepairRepeatBlocks(actions) {
  if (!actions || !actions.length) return actions;
  for (var i = 0; i < actions.length; i++) {
    if (actions[i].type !== "repeat") continue;
    if (scFindMatchingEndRepeat(actions, i) < 0) {
      actions.splice(i + 1, 0, {type: "end_repeat"});
    }
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
    } else if (act.type === "repeat") {
      var endRep = scFindMatchingEndRepeat(actions, i);
      if (endRep < 0) {
        html += scBlockHtml("action", act, i, i === enterIdx);
        i++;
        continue;
      }
      var repEnter = enterIdx >= i && enterIdx <= endRep;
      html += '<div class="sc-if-group' + (repEnter ? " sc-enter" : "") + '">';
      for (var k = i; k <= endRep; k++) {
        html += scBlockHtml("action", scNormalizeAction(actions[k], k), k, k === enterIdx,
          scRepeatBlockMeta(actions, i, endRep, k));
      }
      html += '</div>';
      i = endRep + 1;
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
function scApplyRunSweep(block, idx, opts) {
  opts = opts || {};
  var pct = null;
  if (opts.waitRemaining != null) {
    var total = scRunWaitTotal(idx);
    var left = Math.max(0, Number(opts.waitRemaining) || 0);
    pct = total > 0 ? (1 - left / total) * 100 : 100;
  } else if (opts.nestedStep != null && opts.nestedTotal != null) {
    var nTotal = Number(opts.nestedTotal) || 0;
    var nStep = Math.max(0, Number(opts.nestedStep) || 0);
    if (nTotal > 0) pct = (nStep / nTotal) * 100;
  }
  var el = block.querySelector(".sc-run-wait-sweep");
  if (pct == null) {
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
  el.style.width = Math.max(0, Math.min(100, pct)).toFixed(2) + "%";
}
function scApplyRunWait(block, idx, remaining) {
  scApplyRunSweep(block, idx, {waitRemaining: remaining});
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
  var nestedStep = scRun && scRun.nestedStep != null ? scRun.nestedStep : null;
  var nestedTotal = scRun && scRun.nestedTotal != null ? scRun.nestedTotal : null;
  var nestedRun = scRunActive() && nestedTotal != null && nestedTotal > 0;
  canvas.querySelectorAll('.sc-block[data-block-kind="action"]').forEach(function (block) {
    var idx = Number(block.dataset.blockIdx);
    var current = scRunActive() && idx === step;
    block.classList.toggle("sc-run-current", current);
    var act = scEditing && scEditing.actions ? scEditing.actions[idx] : null;
    if (current && act && act.type === "run_shortcut" && nestedRun) {
      scApplyRunSweep(block, idx, {nestedStep: nestedStep, nestedTotal: nestedTotal});
    } else if (current && scRun.waitRemaining != null) {
      scApplyRunSweep(block, idx, {waitRemaining: scRun.waitRemaining});
    } else {
      scApplyRunSweep(block, idx, null);
    }
  });
  scApplyRunResults(canvas);
}
function scStopRun(notifyHost) {
  if (notifyHost && scRun && scRun.runId) send({cmd: "automationRunStop", runId: scRun.runId});
  scRun = null;
  scApplyRunState();
  scUpdatePlayButton();
}
function scInvalidateTestRunFromEdit() {
  if (!scRun) return;
  if (scRunActive()) {
    scStopRun(true);
    return;
  }
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
  scRun.nestedStep = st.status === "done" || st.nestedStep == null ? null : Number(st.nestedStep);
  scRun.nestedTotal = st.status === "done" || st.nestedTotal == null ? null : Number(st.nestedTotal);
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
  scInvalidateTestRunFromEdit();
}
function scApplyHistorySnapshot(snap) {
  scHistoryApplying = true;
  scInvalidateTestRunFromEdit();
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
function scIsManualEdit() {
  return scEditKind === "manual" || !!(scEditing && scEntryKind(scEditing) === "manual");
}
function scEditorEmptyHtml() {
  if (scIsManualEdit()) {
    return '<div class="sc-ed-empty"><b>Add actions</b>Pick Do actions below to build this shortcut.</div>';
  }
  return '<div class="sc-ed-empty"><b>Start with When</b>Pick a When trigger below to detect chat events, then add actions.</div>';
}
function scRenderEditor(opts) {
  var canvas = $("sc-ed-canvas");
  if (!scEditing) { canvas.innerHTML = ""; return; }
  if (scEditing.trigger) scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  scPruneStaleTextVariables();
  var scroll = canvas.scrollTop;
  var enterIdx = opts && opts.enterActionIdx != null ? opts.enterActionIdx : -1;
  scSyncEditorTitleUI();
  var html = "";
  if (!scIsManualEdit() && scEditing.trigger) {
    html += scBlockHtml("trigger", scEditing.trigger, 0, enterIdx === -2);
  }
  html += scRenderActionBlocks(scEditing.actions || [], enterIdx);
  canvas.innerHTML = html || scEditorEmptyHtml();
  canvas.scrollTop = scroll;
  scSyncPillInputs(canvas);
  scSyncTextareaHeights(canvas);
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
function scGetRepeatMetaForIndex(idx) {
  var actions = scEditing && scEditing.actions;
  if (!actions) return {};
  for (var i = 0; i < actions.length; i++) {
    if (actions[i].type !== "repeat") continue;
    var endIdx = scFindMatchingEndRepeat(actions, i);
    if (endIdx < 0) continue;
    if (idx >= i && idx <= endIdx) return scRepeatBlockMeta(actions, i, endIdx, idx);
  }
  return {};
}
function scGetFlowMetaForIndex(idx) {
  var rep = scGetRepeatMetaForIndex(idx);
  if (rep && rep.role) return rep;
  return scGetIfMetaForIndex(idx);
}
function scUpdateRepeatGroupBlock(idx) {
  if (!scEditing) return;
  var canvas = $("sc-ed-canvas");
  var el = canvas.querySelector('.sc-block[data-block-kind="action"][data-block-idx="' + idx + '"]');
  if (!el) {
    scRenderEditor();
    return;
  }
  var data = scNormalizeAction(scEditing.actions[idx], idx);
  var wrap = document.createElement("div");
  wrap.innerHTML = scBlockHtml("action", data, idx, false, scGetRepeatMetaForIndex(idx));
  var fresh = wrap.firstElementChild;
  if (fresh) el.replaceWith(fresh);
  scSyncPillInputs(canvas);
  scSyncTextareaHeights(canvas);
  scApplyRunState();
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
  scSyncTextareaHeights(canvas);
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
    if (act && (act.type === "repeat" || act.type === "end_repeat" || scIsInsideRepeatGroup(idx))) {
      scUpdateRepeatGroupBlock(idx);
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
  scSyncTextareaHeights(canvas);
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
  if (act.type === "end_if" || act.type === "end_repeat") return;
  scPushHistory();
  if (act.type === "if") {
    var endIdx = scFindMatchingEndIf(scEditing.actions, idx);
    if (endIdx >= 0) scEditing.actions.splice(idx, endIdx - idx + 1);
    else scEditing.actions.splice(idx, 1);
  } else if (act.type === "repeat") {
    var endRep = scFindMatchingEndRepeat(scEditing.actions, idx);
    if (endRep >= 0) scEditing.actions.splice(idx, endRep - idx + 1);
    else scEditing.actions.splice(idx, 1);
  } else {
    scEditing.actions.splice(idx, 1);
  }
  scPruneStaleTextVariables();
  if (!scEditing.actions.length && (!scEditing.trigger || scIsManualEdit())) {
    $("sc-ed-canvas").innerHTML = scEditorEmptyHtml();
  } else {
    scRenderEditor();
  }
}
function scBlockGrip(kind, idx, actType) {
  if (kind === "action") {
    if (actType === "otherwise" || actType === "end_if" || actType === "end_repeat") {
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
  var removeBtn = '<button type="button" class="sc-filter-del" data-remove-filter="' + filterIdx + '" aria-label="Remove filter">\u2212</button>';
  return '<div class="sc-block-filter" data-filter-idx="' + filterIdx + '">' +
    scPill("kind", filter.kind, filterIdx, "trigger-filter") +
    scTxt(opTxt) + valueHtml + removeBtn + '</div>';
}
function scTriggerBlockHtml(data, enter) {
  data = scNormalizeTrigger(JSON.parse(JSON.stringify(data)));
  var enterCls = enter ? " sc-enter" : "";
  var tdef = scTriggerDef(data.type);
  return '<div class="sc-block sc-block-trigger' + enterCls + '" data-block-kind="trigger" data-block-idx="0">' +
    '<div class="sc-block-trigger-main">' +
    '<div class="sc-block-trigger-head">' +
    '<span class="sc-block-ico ' + esc(tdef.tone || "chat") + '">' + scBlockIconInner(tdef.tone || "chat", tdef.icon, "trigger", data.type) + '</span>' +
    '<span class="sc-block-trigger-title">When a chat message is received from others</span>' +
    '</div></div></div>';
}
function scServerConnectBlockHtml(data, enter) {
  data = scNormalizeTrigger(JSON.parse(JSON.stringify(data)));
  var enterCls = enter ? " sc-enter" : "";
  var tdef = scTriggerDef(data.type);
  return '<div class="sc-block sc-block-trigger' + enterCls + '" data-block-kind="trigger" data-block-idx="0">' +
    '<div class="sc-block-trigger-main">' +
    '<div class="sc-block-trigger-head">' +
    '<span class="sc-block-ico ' + esc(tdef.tone || "connect") + '">' + scBlockIconInner(tdef.tone || "connect", tdef.icon, "trigger", data.type) + '</span>' +
    '<span class="sc-block-trigger-title">When connecting to one of the following servers</span>' +
    '</div>' +
    '<div class="sc-block-trigger-divider"></div>' +
    scServerTargetsHtml(data) +
    '</div></div>';
}
function scBlockHtml(kind, data, idx, enter, ifMeta) {
  ifMeta = ifMeta || {};
  var canDel = kind === "action" && data.type !== "end_if" && data.type !== "end_repeat";
  var del = canDel ? '<button class="sc-del" type="button" data-del-action="' + idx + '" aria-label="Remove">' + scIconX() + '</button>' : "";
  var enterCls = enter ? " sc-enter" : "";
  var ifCls = "";
  if (ifMeta.role === "repeat") ifCls = " sc-repeat-head";
  else if (ifMeta.role === "branch") {
    ifCls = " sc-if-branch";
    if (ifMeta.branch) ifCls += " sc-if-branch-" + ifMeta.branch;
  } else if (ifMeta.role === "if") ifCls = " sc-if-head";
  else if (ifMeta.role === "otherwise") ifCls = " sc-if-otherwise";
  else if (ifMeta.role === "end") {
    ifCls = data.type === "end_repeat" ? " sc-repeat-foot" : " sc-if-foot";
  }
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
      '<span class="sc-block-ico ' + esc(adefText.tone || "text") + '">' + scBlockIconInner(adefText.tone || "text", adefText.icon, "action", data.type) + '</span>' +
      '<div class="sc-block-body">' + scTextBlockHtml(data, idx, kind) + '</div>' + del + '</div>';
  } else if (data.type === "connect_server") {
    inner = scTxt("Connect to") + scPillInput("address", data.address, idx, kind);
  } else if (data.type === "leave_server") {
    inner = scTxt("Leave server");
  } else if (data.type === "wait") {
    inner = scTxt("Wait") + scWaitSecondsControl(data.seconds, idx, kind);
  } else if (data.type === "switch_weapon_use") {
    inner = scTxt("Switch to") + scPill("weapon", data.weapon, idx, kind) + scTxt("and use");
  } else if (data.type === "switch_weapon") {
    inner = scTxt("Switch to") + scPill("weapon", data.weapon, idx, kind);
  } else if (data.type === "emote") {
    inner = scTxt("Emote") + scPill("emote", data.emote, idx, kind);
  } else if (data.type === "kill") {
    inner = scTxt("Kill");
  } else if (data.type === "vote") {
    inner = scTxt("Vote") + scPill("choice", data.choice, idx, kind);
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
  } else if (data.type === "get_clipboard") {
    inner = scTxt("Get Clipboard");
  } else if (data.type === "if") {
    scNormalizeIfCondition(data);
    if (scIfIsMulti(data)) {
      inner = scIfMultiBlockHtml(data, idx, kind);
    } else {
      inner = scTxt("If") + scIfLeftFieldHtml(data, idx, kind);
      if (data.left) {
        inner += scPill("op", data.op, idx, kind);
        if (scIfOpNeedsRight(data.op)) {
          if (scIfLeftValueKind(data.left) === "channel") {
            inner += scPill("right", data.right || "all", idx, kind);
          } else if (scIfLeftValueKind(data.left) === "yesno") {
            inner += scPill("right", data.right || "Yes", idx, kind);
          } else {
            inner += scPillInput("right", data.right, idx, kind);
          }
        } else {
          inner += scTxt("value");
        }
        inner += '<button type="button" class="sc-if-cond-add" data-if-expand="' + idx + '" aria-label="Add condition">+</button>';
      }
    }
  } else if (data.type === "otherwise") {
    inner = scTxt("Otherwise");
  } else if (data.type === "end_if") {
    inner = scTxt("End If");
  } else if (data.type === "repeat") {
    inner = scTxt("Repeat") + scRepeatTimesControl(data.count, idx, kind);
  } else if (data.type === "end_repeat") {
    inner = scTxt("End Repeat");
  } else if (data.type === "run_shortcut") {
    inner = scTxt("Run") + scPill("shortcutId", data.shortcutId, idx, kind);
  } else if (data.type === "stop") {
    inner = scTxt("Stop");
  }
  if (kind === "trigger") {
    return scTriggerBlockHtml(data, enter);
  }
  var adef = scActionDefForAction(data);
  var bodyCls = data.type === "if" && scIfIsMulti(data) ? " sc-if-multi-body" : "";
  var bodyInner = data.type === "if" && scIfIsMulti(data) ? inner : '<div class="sc-block-line">' + inner + '</div>';
  var iconType = adef.id || data.type;
  return '<div class="sc-block sc-block-action' + enterCls + ifCls + '" data-block-kind="action" data-block-idx="' + idx + '"' + roleAttr + branchAttr + '>' +
    scBlockGrip(kind, idx, data.type) +
    '<span class="sc-block-ico ' + esc(adef.tone || "action") + '">' + scBlockIconInner(adef.tone || "action", adef.icon, "action", iconType) + '</span>' +
    '<div class="sc-block-body' + bodyCls + '">' + bodyInner + '</div>' + del + '</div>';
}
function scTxt(text) {
  return '<span class="sc-txt">' + esc(text) + '</span>';
}
function scPill(field, value, idx, kind, condIdx) {
  var label = String(value || "");
  if (field === "kind") {
    if (value === "message") label = "Message";
    else if (value === "chat_channel") label = "Chat channel";
    else if (value === "uclient_room") label = "UClient room";
    else label = "Sender";
  }
  if (field === "channel") label = scLabelChannel(value);
  if (field === "match") {
    label = kind === "action" ? (value === "any" ? "Any" : "All")
      : (value === "equals" ? "equals" : value === "starts_with" ? "starts with" : "contains");
  }
  if (field === "weapon") label = scLabelWeapon(value);
  if (field === "target") label = value === "dummy" ? "Dummy" : "Player";
  if (field === "enabled") label = value === "on" || value === true ? "On" : "Off";
  if (field === "property") label = scLabelGetProperty(value);
  if (field === "emote") label = scLabelEmote(value);
  if (field === "choice") label = scLabelVoteChoice(value);
  if (field === "shortcutId") label = scRunShortcutLabel(value);
  if (field === "left") label = value ? (scLabelVariable(value) || scLabelGetProperty(value)) : "Condition";
  if (field === "op") label = scLabelIfOp(value);
  if (field === "right") {
    var ifRef = scGetBlockRef(kind, idx, condIdx);
    var ifLeft = ifRef ? ifRef.left : "";
    label = scIfLeftValueKind(ifLeft) === "yesno" ? scLabelYesNoValue(value) : String(value || "");
  }
  var isVar = field === "messageVariable" || field === "textPartVar" ||
    field === "channelVariable" || field === "uclientRoomVariable";
  if (isVar) {
    return scVarPillButton("", value, label, scPillDataAttrs(field, idx, kind, condIdx));
  }
  return '<button type="button" class="sc-pill"' + scPillDataAttrs(field, idx, kind, condIdx) + '>' + esc(label) + '</button>';
}
function scPillDataAttrs(field, idx, kind, condIdx) {
  var a = ' data-field="' + esc(field) + '" data-idx="' + idx + '" data-kind="' + esc(kind) + '"';
  if (condIdx != null && condIdx >= 0) a += ' data-cond-idx="' + condIdx + '"';
  return a;
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
function scPillInput(field, value, idx, kind, subIdx, extraClass, condIdx) {
  var val = esc(String(value == null ? "" : value));
  var mode = field === "seconds" ? ' inputmode="numeric"' : (field === "color" ? ' inputmode="decimal"' : "");
  var ph = scPillInputPlaceholder(field);
  var phAttr = ph ? ' placeholder="' + esc(ph) + '"' : "";
  var subAttr = subIdx != null ? ' data-name-idx="' + subIdx + '"' : "";
  if (condIdx != null && condIdx >= 0) subAttr += ' data-cond-idx="' + condIdx + '"';
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
  m.style.whiteSpace = "pre";
  m.textContent = text.length ? text : " ";
  return m.getBoundingClientRect().width;
}
function scMeasureTextSegment(ta, text) {
  var lines = String(text || "").split("\n");
  var longest = " ";
  for (var i = 0; i < lines.length; i++) {
    if (lines[i].length >= longest.length) longest = lines[i].length ? lines[i] : " ";
  }
  return scMeasurePillInput(ta, longest);
}
function scTextSegmentOneLineHeight(ta) {
  var cs = getComputedStyle(ta);
  var lh = parseFloat(cs.lineHeight);
  if (!isFinite(lh)) lh = (parseFloat(cs.fontSize) || 15) * 1.45;
  return Math.ceil(lh + (parseFloat(cs.paddingTop) || 0) + (parseFloat(cs.paddingBottom) || 0));
}
function scGrowTextareaHeight(ta, minH, maxH) {
  ta.style.height = "auto";
  var next = Math.min(maxH, Math.max(minH, ta.scrollHeight));
  ta.style.height = next + "px";
  ta.style.overflowY = ta.scrollHeight > maxH ? "auto" : "hidden";
}
function scAutoGrowTextarea(ta) {
  if (!ta || ta.tagName !== "TEXTAREA") return;
  var minH = ta.classList.contains("sc-text-segment-input") ? 22 : 44;
  var maxH = 240;
  scGrowTextareaHeight(ta, minH, maxH);
}
function scInlineSegmentMaxWidth(ta) {
  var composer = ta.closest(".sc-text-composer");
  if (!composer) return 400;
  var gap = 0;
  var total = Math.max(48, composer.clientWidth - 24);
  var used = 0;
  var child = composer.firstElementChild;
  while (child) {
    if (child === ta) break;
    if (child.classList && child.classList.contains("sc-text-var-bar")) {
      child = child.nextElementSibling;
      continue;
    }
    used += child.getBoundingClientRect().width + gap;
    child = child.nextElementSibling;
  }
  return Math.max(48, total - used);
}
function scComposerTextSegmentCount(composer) {
  return composer ? composer.querySelectorAll("textarea.sc-text-segment-input").length : 0;
}
function scTextSegmentAdjacentVar(ta) {
  var prev = ta.previousElementSibling;
  var next = ta.nextElementSibling;
  return {
    before: !!(prev && prev.classList && prev.classList.contains("sc-text-var-inline")),
    after: !!(next && next.classList && next.classList.contains("sc-text-var-inline"))
  };
}
function scFitTextSegment(ta) {
  if (!ta || !ta.classList.contains("sc-text-segment-input")) {
    scAutoGrowTextarea(ta);
    return;
  }
  var val = ta.value || "";
  var composer = ta.closest(".sc-text-composer");
  var oneLine = scTextSegmentOneLineHeight(ta);
  var minH = Math.max(22, oneLine);
  var maxH = 240;
  var rowMax = scInlineSegmentMaxWidth(ta);
  var ph = ta.getAttribute("placeholder") || "";
  var isEmpty = !val.length;
  var multiSeg = scComposerTextSegmentCount(composer) > 1;
  var hasInlineVar = composer && composer.querySelector(".sc-text-var-inline");
  var adj = scTextSegmentAdjacentVar(ta);
  var inlineFlow = adj.before || adj.after;
  var contentW = scMeasureTextSegment(ta, val.length ? val : ph);
  var inlineW;
  if (isEmpty && !hasInlineVar && !multiSeg) {
    inlineW = rowMax;
  } else if (isEmpty && multiSeg) {
    inlineW = document.activeElement === ta ? 12 : 4;
  } else {
    inlineW = Math.max(isEmpty ? 48 : 8, Math.min(rowMax, Math.ceil(contentW)));
  }
  if (val.indexOf("\n") >= 0) inlineW = rowMax;
  ta.style.flex = "0 1 auto";
  ta.style.maxWidth = rowMax + "px";
  ta.style.width = inlineW + "px";
  scGrowTextareaHeight(ta, minH, maxH);
  if (!inlineFlow && val.length > 0 && val.indexOf("\n") < 0 && ta.scrollHeight > oneLine + 2 && inlineW < rowMax) {
    ta.style.width = rowMax + "px";
    scGrowTextareaHeight(ta, minH, maxH);
  } else if (inlineFlow && val.length > 0 && val.indexOf("\n") < 0 && ta.scrollHeight > oneLine + 2) {
    ta.style.width = inlineW + "px";
    scGrowTextareaHeight(ta, minH, maxH);
  }
}
function scSyncTextareaHeights(root) {
  var el = root || $("sc-ed-canvas");
  el.querySelectorAll(".sc-text-composer").forEach(function (composer) {
    composer.querySelectorAll("textarea.sc-text-segment-input").forEach(scFitTextSegment);
  });
}
function scFitPillInput(input) {
  if (!input) return;
  if (input.tagName === "TEXTAREA") {
    scAutoGrowTextarea(input);
    return;
  }
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
  el.querySelectorAll("textarea.sc-text-segment-input").forEach(function (input) {
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
    scInvalidateTestRunFromEdit();
    return;
  }
  if (field === "serverTarget" && kind === "trigger") {
    scCommitServerTargetInput(Number(input.dataset.nameIdx || 0), input.value);
    scInvalidateTestRunFromEdit();
    return;
  }
  var condIdx = input.dataset.condIdx != null ? Number(input.dataset.condIdx) : null;
  var ref = scGetBlockRef(kind, idx, condIdx);
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
    if (input.classList.contains("sc-text-segment-input")) scCaptureTextPartsFromDom(idx, ref);
    else {
      var pi = Number(input.dataset.nameIdx || 0);
      if (!ref.parts[pi]) ref.parts[pi] = {mode: "text", text: ""};
      ref.parts[pi] = {mode: "text", text: val};
      ref.parts = scMergeTextParts(ref.parts);
    }
  } else {
    ref[field] = val;
    if (field === "right" || field === "left") scNormalizeIfConditionObj(ref);
  }
  if (input.tagName === "TEXTAREA") {
    if (input.classList.contains("sc-text-segment-input")) scFitTextSegment(input);
    else scAutoGrowTextarea(input);
  } else scFitPillInput(input);
  if (kind === "trigger-filter") scRememberFilterDraft(idx);
  else if (kind === "if-cond") scUpdateIfGroupBlock(idx);
  scInvalidateTestRunFromEdit();
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
  if (filterIdx < 0) return;
  scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  if (filterIdx >= scEditing.trigger.filters.length) return;
  scPushHistory();
  scCommitFilterRowInputs(filterIdx);
  scEditing.trigger.filters.splice(filterIdx, 1);
  scReindexFilterDrafts(filterIdx);
  scUpdateBlockElement("trigger", 0);
}
function scGetIfConditionRef(ifIdx, condIdx) {
  var act = scEditing && scEditing.actions ? scEditing.actions[ifIdx] : null;
  if (!act || act.type !== "if" || !act.conditions) return null;
  return act.conditions[condIdx] || null;
}
function scGetBlockRef(kind, idx, condIdx) {
  if (!scEditing) return null;
  if (kind === "trigger-filter") return scGetFilterRef(idx);
  if (kind === "if-cond") return scGetIfConditionRef(idx, condIdx != null ? condIdx : 0);
  return kind === "trigger" ? scEditing.trigger : scEditing.actions[idx];
}
function scSetBlockField(kind, idx, field, value, condIdx) {
  scPushHistory();
  if (kind === "trigger-filter") {
    if (field === "kind") scSetFilterKind(idx, value);
    else scSetFilterField(idx, field, value);
    return;
  }
  var ref = scGetBlockRef(kind, idx, condIdx);
  if (!ref) return;
  if (field === "seconds") ref.seconds = scClampWaitSeconds(value);
  else if (field === "color") ref.color = Number(value) || 0;
  else if (field === "enabled") ref[field] = value === "on" || value === "true" || value === true;
  else if (field === "match") {
    var ifAct = scEditing.actions[idx];
    if (ifAct && ifAct.type === "if") ifAct.match = value === "any" ? "any" : "all";
  } else if (field === "left") {
    ref.left = value;
    scNormalizeIfConditionObj(ref);
  } else if (field === "op") {
    ref.op = value;
    scNormalizeIfConditionObj(ref);
  } else if (field === "right") {
    ref.right = value;
    scNormalizeIfConditionObj(ref);
  } else if (field === "property" && ref.type === "get") {
    var oldVar = scGetPropertyVarId(ref.property);
    ref[field] = value;
    ref.property = scNormalizeGetProperty(ref.property);
    var newVar = scGetPropertyVarId(ref.property);
    if (oldVar !== newVar) scRemapVariableIdInShortcut(oldVar, newVar);
  } else ref[field] = value;
  if (kind === "if-cond") scUpdateIfGroupBlock(idx);
  else scUpdateBlockElement(kind, idx);
}
function scOptionsForField(field, actionIdx, condIdx) {
  var ref = null;
  if (condIdx != null && condIdx >= 0) ref = scGetIfConditionRef(actionIdx, condIdx);
  else ref = actionIdx != null && scEditing && scEditing.actions ? scEditing.actions[actionIdx] : null;
  if (field === "channel") return [{value: "all", label: "All"}, {value: "team", label: "Team"}, {value: "uclient", label: "UClient"}];
  if (field === "match") {
    var ifAct = actionIdx != null && scEditing && scEditing.actions ? scEditing.actions[actionIdx] : null;
    if (ifAct && ifAct.type === "if" && scIfIsMulti(ifAct)) {
      return [{value: "all", label: "All"}, {value: "any", label: "Any"}];
    }
    return [{value: "contains", label: "contains"}, {value: "equals", label: "equals"}, {value: "starts_with", label: "starts with"}];
  }
  if (field === "weapon") return [{value: "hammer", label: "Hammer"}, {value: "gun", label: "Gun"}, {value: "shotgun", label: "Shotgun"}, {value: "grenade", label: "Grenade"}, {value: "laser", label: "Laser"}];
  if (field === "target") return [{value: "player", label: "Player"}, {value: "dummy", label: "Dummy"}];
  if (field === "enabled") return [{value: "on", label: "On"}, {value: "off", label: "Off"}];
  if (field === "property") {
    var group = "os";
    if (actionIdx != null && scEditing && scEditing.actions && scEditing.actions[actionIdx]) {
      group = scGetPropertyGroup(scEditing.actions[actionIdx].property);
    }
    return group === "game" ? scGetGamePropertyOptions() : scGetOsPropertyOptions();
  }
  if (field === "emote") {
    return [{value: "normal", label: "Normal"}, {value: "happy", label: "Happy"}, {value: "angry", label: "Angry"},
      {value: "pain", label: "Pain"}, {value: "surprise", label: "Surprise"}, {value: "blink", label: "Blink"}];
  }
  if (field === "choice") return [{value: "yes", label: "Yes"}, {value: "no", label: "No"}];
  if (field === "op" && ref && ref.left) return scIfOpsForVariable(ref.left || "");
  if (field === "right" && ref && ref.left && scIfLeftValueKind(ref.left || "") === "channel") {
    return [{value: "all", label: "All"}, {value: "team", label: "Team"}, {value: "uclient", label: "UClient"}];
  }
  if (field === "right" && ref && ref.left && scIfLeftValueKind(ref.left || "") === "yesno") {
    return [{value: "Yes", label: "Yes"}, {value: "No", label: "No"}];
  }
  if (field === "shortcutId") {
    return scRunnableManualShortcuts(scEditing && scEditing.id).map(function (sc) {
      return {value: sc.id, label: scRunShortcutLabel(sc.id)};
    });
  }
  return [];
}
function scLoadEditorDocument(existing, editKind) {
  scEditKind = editKind || (existing ? scEntryKind(existing) : (scLibraryTab === "shortcuts" ? "manual" : "automation"));
  scEditing = existing ? JSON.parse(JSON.stringify(existing)) : {
    id: scUuid(), name: "New Shortcut", enabled: true,
    kind: scEditKind === "manual" ? "manual" : "automation",
    trigger: null, actions: []
  };
  if (scEditing && !scEditing.kind) scEditing.kind = scEntryKind(scEditing);
  if (scIsManualEdit()) scEditing.trigger = null;
  if (scEditing.trigger) scEditing.trigger = scPrepareTriggerForEdit(scEditing.trigger);
  if (scEditing.actions) {
    scEditing.actions = scRepairRepeatBlocks(scRepairIfBlocks(scEditing.actions.map(function (a, i) { return scNormalizeAction(a, i); })));
  }
  scSeedFilterDrafts();
  scResetHistory();
  scRun = null;
  scUpdatePlayButton();
  $("sc-ed-delete").style.display = existing ? "grid" : "none";
  scDrawerFilter = null;
  var drawerSearch = $("sc-drawer-search");
  if (drawerSearch) drawerSearch.value = "";
  scRenderEditor();
  scSyncDrawerSearchUI();
  scRenderDrawer();
}
function scOpenEditor(existing, editKind) {
  scEditorAnimGen++;
  scLoadEditorDocument(existing, editKind);
  var ed = $("sc-editor");
  var view = $("shortcuts-view");
  ed.classList.remove("on");
  view.classList.add("editing");
  ed.style.display = "flex";
  ed.setAttribute("aria-hidden", "false");
  requestAnimationFrame(function () {
    scRelayoutDrawer();
    requestAnimationFrame(function () {
      ed.classList.add("on");
      scRelayoutDrawer();
    });
  });
}
function scAnimateEditorSwap(existing, editKind) {
  scCloseAllPickers(null);
  scCommitAllPillInputs($("sc-ed-canvas"));
  scEndDrag(true);
  if (scRunActive()) scStopRun(true);
  var ed = $("sc-editor");
  if (ed.style.display !== "flex" || !ed.classList.contains("on")) {
    scOpenEditor(existing, editKind);
    return;
  }
  var gen = ++scEditorAnimGen;
  ed.classList.remove("on");
  var finished = false;
  function revealNext() {
    if (finished || gen !== scEditorAnimGen) return;
    finished = true;
    ed.removeEventListener("transitionend", onEnd);
    scLoadEditorDocument(existing, editKind);
    requestAnimationFrame(function () {
      requestAnimationFrame(function () {
        if (gen !== scEditorAnimGen) return;
        ed.classList.add("on");
        scRelayoutDrawer();
      });
    });
  }
  function onEnd(e) {
    if (e.target !== ed) return;
    if (e.propertyName !== "transform" && e.propertyName !== "-webkit-transform") return;
    revealNext();
  }
  ed.addEventListener("transitionend", onEnd);
  setTimeout(revealNext, 480);
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
var SC_DRAWER_STAGE2_VISIBLE = 100;
var SC_DRAWER_CAP_W_MAX = 340;
var SC_DRAWER_CAP_W_MARGIN = 72;
var SC_DRAWER_CAP_H = 100;
var SC_DRAWER_CAP_BOTTOM = 12;
var SC_DRAWER_RUBBER = 0.16;
var SC_DRAWER_FLICK_PX_S = 520;
var SC_DRAWER_FLICK_WINDOW_MS = 110;
var SC_DRAWER_FLICK_PROJECT_S = 0.16;
var scDrawerY = SC_DRAWER_PEEK;
var scDrawerRawY = SC_DRAWER_PEEK;
var scDrawerFullHeight = 520;
var scDrawerRelayoutRaf = 0;
function scDrawerHostMetrics(drawer) {
  var body = drawer && drawer.parentElement;
  var w = body && body.clientWidth ? body.clientWidth : 0;
  var h = body && body.clientHeight ? body.clientHeight : 0;
  if (!w || !h) {
    var ed = $("sc-editor");
    if (ed && ed.clientWidth && ed.clientHeight) {
      var head = ed.querySelector(".sc-ed-head");
      var headH = head ? head.offsetHeight : 0;
      if (!w) w = ed.clientWidth;
      if (!h) h = Math.max(0, ed.clientHeight - headH);
    }
  }
  return {w: w || 0, h: h || 560};
}
function scScheduleDrawerRelayout() {
  if (scDrawerRelayoutRaf) return;
  scDrawerRelayoutRaf = requestAnimationFrame(function () {
    scDrawerRelayoutRaf = 0;
    requestAnimationFrame(function () { scRelayoutDrawer(); });
  });
}
function scRelayoutDrawer() {
  var ed = $("sc-editor");
  if (!ed || ed.style.display !== "flex") return;
  scSetDrawerY(scDrawerStage2Ratio(), false);
}
function scDrawerFullHeightPx(drawer) {
  var parentH = scDrawerHostMetrics(drawer).h;
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
  var host = scDrawerHostMetrics(drawer);
  var parentW = host.w;
  if (parentW < 80) scScheduleDrawerRelayout();
  if (!parentW) parentW = host.w || 360;
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
    ? "0 0 0 0.5px rgba(255,255,255," + (0.028 * t).toFixed(3) + "),0 0 " + (36 * t).toFixed(0) + "px rgba(255,255,255," + (0.038 * t).toFixed(3) + "),0 0 " + (72 * t).toFixed(0) + "px rgba(255,255,255," + (0.022 * t).toFixed(3) + "),0 " + (10 * t).toFixed(0) + "px " + (36 * t).toFixed(0) + "px rgba(255,255,255," + (0.012 * t).toFixed(3) + ")"
    : "none";
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
  if (kind === "trigger" && scIsManualEdit()) return;
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
    if (copy.type === "run_shortcut") {
      var runList = scRunnableManualShortcuts(scEditing.id);
      copy.shortcutId = runList.length ? runList[0].id : "";
      if (!runList.length) scToast("Create a Shortcut with actions first");
    }
    if (copy.type === "if") {
      var startIdx = scEditing.actions.length;
      scEditing.actions.push(copy);
      scEditing.actions.push({type: "otherwise"});
      scEditing.actions.push({type: "end_if"});
      scRenderEditor({enterActionIdx: startIdx});
    } else if (copy.type === "repeat") {
      var repStart = scEditing.actions.length;
      scEditing.actions.push(copy);
      scEditing.actions.push({type: "end_repeat"});
      scRenderEditor({enterActionIdx: repStart});
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
function scMoveRepeatGroup(fromRepeatIdx, toIdx) {
  if (!scEditing || !scEditing.actions) return;
  var endIdx = scFindMatchingEndRepeat(scEditing.actions, fromRepeatIdx);
  if (endIdx < 0) return;
  if (toIdx === fromRepeatIdx || toIdx === endIdx + 1) return;
  scPushHistory();
  var len = endIdx - fromRepeatIdx + 1;
  var group = scEditing.actions.splice(fromRepeatIdx, len);
  if (toIdx > fromRepeatIdx) toIdx -= len;
  toIdx = Math.max(0, Math.min(toIdx, scEditing.actions.length));
  Array.prototype.splice.apply(scEditing.actions, [toIdx, 0].concat(group));
  scRenderEditor();
}
function scMoveAction(fromIdx, toIdx) {
  if (!scEditing || !scEditing.actions) return;
  if (fromIdx < 0 || toIdx < 0) return;
  if (fromIdx >= scEditing.actions.length) return;
  var act = scEditing.actions[fromIdx];
  if (act.type === "otherwise" || act.type === "end_if" || act.type === "end_repeat") return;
  if (act.type === "if") {
    scMoveIfGroup(fromIdx, toIdx);
    return;
  }
  if (act.type === "repeat") {
    scMoveRepeatGroup(fromIdx, toIdx);
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
function scCanvasDropWouldMove(drag, insertIdx) {
  if (!drag || insertIdx == null) return false;
  var from = drag.idx;
  if (drag.groupEnd != null && drag.groupEnd >= 0) {
    if (insertIdx === from) return false;
    if (insertIdx > from && insertIdx <= drag.groupEnd + 1) return false;
    return true;
  }
  return insertIdx !== from && insertIdx !== from + 1;
}
function scDropGapInIfBranch(insertIdx, targetBlock) {
  if (insertIdx != null && scIsInsideIfGroup(insertIdx)) return true;
  if (insertIdx != null && scIsInsideRepeatGroup(insertIdx)) return true;
  if (!targetBlock) return false;
  if (targetBlock.classList.contains("sc-if-branch")) return true;
  if (targetBlock.classList.contains("sc-if-otherwise")) return true;
  if (targetBlock.classList.contains("sc-if-foot")) return true;
  if (targetBlock.classList.contains("sc-repeat-foot")) return true;
  if (!scEditing || !scEditing.actions) return false;
  var act = scEditing.actions[insertIdx];
  if (!act) return false;
  if (act.type === "otherwise" || act.type === "end_if" || act.type === "end_repeat") return true;
  if (insertIdx > 0) {
    var prev = scEditing.actions[insertIdx - 1];
    if (prev && (prev.type === "if" || prev.type === "repeat")) return true;
  }
  return false;
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
  if (scDropGapInIfBranch(insertIdx, target)) gap.classList.add("sc-drop-gap-in-if");
  if (!target) {
    canvas.appendChild(gap);
    return;
  }
  var parent = target.parentElement || canvas;
  parent.insertBefore(gap, target);
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
      if (moved) {
        if (scDrag.groupKind === "repeat") scMoveRepeatGroup(scDrag.idx, scDrag.overIdx);
        else scMoveIfGroup(scDrag.idx, scDrag.overIdx);
      }
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
    scBindCanvasDropGap(scCanvasDropWouldMove(scDrag, scDrag.overIdx) ? scDrag.overIdx : null);
  }
});
document.addEventListener("pointerup", function () { scClearStepperHold(); scEndDrag(false); });
document.addEventListener("pointercancel", function () { scEndDrag(true); });
function scDrawerFilterMeta() {
  var def = scDrawerFilter ? scDrawerFilterDef(scDrawerFilter) : null;
  if (!def) return null;
  return {label: def.label, svg: scDrawerFilterSvg(def.id)};
}
function scSyncDrawerSearchUI() {
  var chip = $("sc-drawer-search-chip");
  var input = $("sc-drawer-search");
  if (!chip || !input) return;
  var meta = scDrawerFilterMeta();
  if (meta) {
    chip.classList.add("is-on");
    chip.innerHTML = '<span class="ico" aria-hidden="true">' + meta.svg + '</span>' + esc(meta.label);
    input.placeholder = "Search";
  } else {
    chip.classList.remove("is-on");
    chip.textContent = "";
    input.placeholder = "Search actions";
  }
}
function scSetDrawerFilter(id) {
  if (!scDrawerFilterDef(id)) return;
  if (scIsManualEdit() && id === "automation") return;
  scDrawerFilter = id;
  scSyncDrawerSearchUI();
  scExpandDrawerToPeekFromSearch();
  scRenderDrawer();
  var input = $("sc-drawer-search");
  if (input) input.focus();
}
function scClearDrawerFilter() {
  if (!scDrawerFilter) return;
  scDrawerFilter = null;
  scSyncDrawerSearchUI();
  scRenderDrawer();
}
function scRenderDrawer() {
  if (scIsManualEdit() && scDrawerFilter === "automation") scDrawerFilter = null;
  scSyncDrawerSearchUI();
  var chipsEl = $("sc-drawer-chips");
  if (scDrawerFilter) {
    chipsEl.innerHTML = "";
    chipsEl.style.display = "none";
  } else {
    chipsEl.style.display = "";
    var picks = SC_DRAWER_FILTERS.filter(function (p) {
      return !p.automationOnly || !scIsManualEdit();
    });
    chipsEl.innerHTML = picks.map(function (p) {
      return '<button type="button" class="sc-drawer-filter-pick" data-drawer-filter="' + p.id + '"><span class="ico" aria-hidden="true">' + scDrawerFilterSvg(p.id) + '</span>' + esc(p.label) + '</button>';
    }).join("");
  }
  var q = ($("sc-drawer-search").value || "").toLowerCase();
  var html = "";
  scDrawerTriggerSections().forEach(function (sec) {
    var rows = SC_TRIGGERS.filter(function (t) {
      return t.category === sec.id && scCatalogMatches(t, q, true);
    });
    if (!rows.length) return;
    html += '<div class="sc-drawer-section">' + esc(sec.label) + '</div>';
    rows.forEach(function (t) { html += scCatalogRow("trigger", t); });
  });
  scDrawerActionSections().forEach(function (sec) {
    var rows = SC_ACTIONS.filter(function (a) {
      if (a.id === "otherwise" || a.id === "end_if" || a.id === "end_repeat") return false;
      return (a.category || "actions") === sec.id && scCatalogMatches(a, q, false);
    });
    if (!rows.length) return;
    html += '<div class="sc-drawer-section">' + esc(sec.label) + '</div>';
    rows.forEach(function (a) { html += scCatalogRow("action", a); });
  });
  $("sc-drawer-list").innerHTML = html || '<div class="sc-ed-empty" style="margin-top:24px"><b>No matches</b>Try another search or filter.</div>';
}
$("sc-fab-manual").addEventListener("click", function () { scOpenEditor(null, "manual"); });
$("sc-fab-auto").addEventListener("click", function () { scOpenEditor(null, "automation"); });
document.querySelectorAll(".sc-lib-tab").forEach(function (btn) {
  btn.addEventListener("click", function () { setScLibraryTab(btn.dataset.scLib); });
});
window.addEventListener("resize", function () {
  scLayoutLibTabIndicator();
  if ($("sc-editor").style.display === "flex") scRelayoutDrawer();
});
requestAnimationFrame(scLayoutLibTabIndicator);
$("sc-gallery-search").addEventListener("input", function (e) {
  scGallerySearch = e.target.value || "";
  renderShortcutsGallery();
});
$("sc-gallery-grid").addEventListener("click", function (e) {
  var play = e.target.closest("[data-play]");
  if (play) {
    e.stopPropagation();
    e.preventDefault();
    var runSc = shortcutsLocal.find(function (s) { return s.id === play.dataset.play; });
    scRunShortcutActions(runSc);
    return;
  }
  var tile = e.target.closest(".sc-tile[data-id]");
  if (!tile) return;
  var sc = shortcutsLocal.find(function (s) { return s.id === tile.dataset.id; });
  if (!sc) return;
  scOpenEditor(sc, "manual");
});
$("sc-ed-back").addEventListener("click", function () { scCloseEditor(); });
$("sc-ed-save").addEventListener("click", function () {
  if (!scEditing) return;
  if (!scEditing.actions || !scEditing.actions.length) { scToast("Add at least one action"); return; }
  scCommitAllPillInputs($("sc-ed-canvas"));
  if (scIsManualEdit()) {
    scEditing.kind = "manual";
    scEditing.trigger = null;
  } else {
    if (!scEditing.trigger) { scToast("Add a When trigger"); return; }
    scEditing.kind = "automation";
    scEditing.trigger = scCleanTriggerForSave(scEditing.trigger);
  }
  scEditing.actions = (scEditing.actions || []).map(scCleanActionForSave);
  var rawName = $("sc-ed-title").value.trim() ||
    (scIsManualEdit() ? "Shortcut" : scSummaryTrigger(scEditing.trigger));
  scEditing.name = scIsManualEdit() ? scUniqueManualShortcutName(rawName, scEditing.id) : rawName;
  var idx = shortcutsLocal.findIndex(function (s) { return s.id === scEditing.id; });
  if (idx >= 0) shortcutsLocal[idx] = JSON.parse(JSON.stringify(scEditing));
  else shortcutsLocal.push(JSON.parse(JSON.stringify(scEditing)));
  shortcutsSig = JSON.stringify(shortcutsLocal);
  scSaveAll();
  renderShortcutsGallery();
  renderShortcutsList();
  scCloseEditor();
});
$("sc-ed-delete").addEventListener("click", scDeleteEditingShortcut);
$("sc-ed-title-trigger").addEventListener("click", function (e) {
  e.stopPropagation();
  scOpenEditorTitleMenu();
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
$("sc-ed-title").addEventListener("focusout", function () {
  scEndEditorRename();
});
$("sc-ed-title").addEventListener("keydown", function (e) {
  if (e.key === "Enter") {
    e.preventDefault();
    this.blur();
  }
  if (e.key === "Escape") {
    e.preventDefault();
    if (scEditing) this.value = scEditing.name || "";
    this.blur();
  }
});
$("sc-ed-title").addEventListener("input", function (e) {
  if (!scEditing) return;
  if (!scTitleDirty) {
    scTitleDirty = true;
    scPushHistory();
  }
  scEditing.name = e.target.value;
  scInvalidateTestRunFromEdit();
  var label = $("sc-ed-title-label");
  if (label) label.textContent = e.target.value || scEditorDisplayName();
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
  scRun = {runId: scUuid(), status: "running", step: -1, total: actions.length, waitRemaining: null, nestedStep: null, nestedTotal: null, results: []};
  scApplyRunState();
  scUpdatePlayButton();
  send({cmd: "automationRun", runId: scRun.runId, actions: actions});
});
function scExpandDrawerForSearchInteraction() {
  scExpandDrawerToPeekFromSearch();
}
var scDrawerSearchWrap = $("sc-drawer-search-wrap");
if (scDrawerSearchWrap) {
  scDrawerSearchWrap.addEventListener("pointerdown", function (e) {
    if (e.button !== 0) return;
    scExpandDrawerForSearchInteraction();
  });
}
$("sc-drawer-search").addEventListener("focusin", scExpandDrawerForSearchInteraction);
$("sc-drawer-search").addEventListener("input", function () {
  scExpandDrawerForSearchInteraction();
  scRenderDrawer();
});
$("sc-drawer-search").addEventListener("keydown", function (e) {
  if (e.key !== "Backspace" && e.keyCode !== 8) return;
  if ((this.value || "").length) return;
  if (!scDrawerFilter) return;
  e.preventDefault();
  scClearDrawerFilter();
});
$("sc-drawer-chips").addEventListener("click", function (e) {
  var pick = e.target.closest("[data-drawer-filter]");
  if (!pick) return;
  scSetDrawerFilter(pick.dataset.drawerFilter);
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
  var groupEnd = -1;
  var groupKind = null;
  if (act && act.type === "if") {
    groupEnd = scFindMatchingEndIf(scEditing.actions, idx);
    groupKind = "if";
  } else if (act && act.type === "repeat") {
    groupEnd = scFindMatchingEndRepeat(scEditing.actions, idx);
    groupKind = "repeat";
  }
  scDrag = {type: "block", idx: idx, groupEnd: groupEnd, groupKind: groupKind, label: block ? block.textContent.trim().slice(0, 48) : "Action", el: block, overIdx: idx};
  if (block) block.classList.add("dragging");
  scEnsureDragGhost(scDrag.label);
  grip.setPointerCapture(e.pointerId);
  e.preventDefault();
});
$("sc-ed-canvas").addEventListener("compositionstart", function (e) {
  var ta = e.target.closest("textarea.sc-text-segment-input");
  if (ta) ta.dataset.scComposing = "1";
}, true);
$("sc-ed-canvas").addEventListener("compositionend", function (e) {
  var ta = e.target.closest("textarea.sc-text-segment-input");
  if (ta) {
    delete ta.dataset.scComposing;
    scRememberTextCaret(ta);
    scFitTextSegment(ta);
  }
}, true);
$("sc-ed-canvas").addEventListener("mouseup", function (e) {
  var ta = e.target.closest("textarea.sc-text-segment-input");
  if (ta) requestAnimationFrame(function () { scRememberTextCaret(ta); });
});
$("sc-ed-canvas").addEventListener("keyup", function (e) {
  var ta = e.target.closest("textarea.sc-text-segment-input");
  if (ta && !e.isComposing) scRememberTextCaret(ta);
});
$("sc-ed-canvas").addEventListener("focusin", function (e) {
  var input = e.target.closest("input.sc-pill.input");
  var textarea = e.target.closest("textarea.sc-text-segment-input");
  if (textarea) {
    textarea.dataset.scOrig = textarea.value;
    scFitTextSegment(textarea);
    scRememberTextCaret(textarea);
    var composer = textarea.closest(".sc-text-composer");
    scTextVarPopState = {
      anchor: composer, el: textarea, blockIdx: textarea.dataset.idx, kind: textarea.dataset.kind || "action",
      partIdx: textarea.dataset.nameIdx
    };
    scCloseAllSmartMenus(null);
    scClosePop();
    scShowTextVarPopForBlock(Number(textarea.dataset.idx || 0), textarea.dataset.kind);
    return;
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
  var textarea = e.target.closest("textarea.sc-text-segment-input");
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
  if (input.dataset.input === "textPart" && input.classList.contains("sc-text-segment-input")) {
    var tIdx = Number(input.dataset.idx || 0);
    var tRef = scGetBlockRef(input.dataset.kind, tIdx);
    if (tRef) {
      scCaptureTextPartsFromDom(tIdx, tRef);
    }
    scFitTextSegment(input);
    scRememberTextCaret(input);
  }
  if (input.classList.contains("sc-smart-trigger")) {
    var field = input.closest(".sc-smart-field");
    if (String(input.value || "").length === 0) scSmartFieldShowMenu(field, true);
    else scSmartFieldHideMenu(field);
  }
  if (input.tagName !== "TEXTAREA") scFitPillInput(input);
});
var scTextVarPopEl = $("sc-text-var-pop");
if (scTextVarPopEl) {
  scTextVarPopEl.addEventListener("mousedown", function (e) {
    if (e.target.closest(".sc-text-var-chip")) e.preventDefault();
  });
  scTextVarPopEl.addEventListener("click", function (e) {
    var varChip = e.target.closest(".sc-text-var-chip");
    if (!varChip || !scEditing) return;
    e.preventDefault();
    scAppendTextVariable(Number(varChip.dataset.idx), varChip.dataset.kind, varChip.dataset.textVar);
  });
}
$("sc-ed-canvas").addEventListener("focusout", function (e) {
  var input = e.target.closest("input.sc-pill.input");
  var textarea = e.target.closest("textarea.sc-text-segment-input");
  if (textarea) {
    scCommitPillInput(textarea, false);
    var composer = textarea.closest(".sc-text-composer");
    setTimeout(function () {
      var pop = $("sc-text-var-pop");
      if (pop && pop.contains(document.activeElement)) return;
      var active = document.activeElement;
      if (active && active.closest && active.closest(".sc-text-composer")) return;
      if (active && active.closest && active.closest("#sc-text-var-pop")) return;
      if (active && active.closest && active.closest(".sc-text-var-bar")) return;
      if (scTextVarPopState.anchor === composer) scHideTextVarPop();
    }, 150);
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
    var portaled = smartField._scMenuPortaled;
    if (portaled && (portaled.contains(active) || portaled.matches(":hover"))) return;
    scSmartFieldHideMenu(smartField);
  }, 130);
});
$("sc-ed-canvas").addEventListener("keydown", function (e) {
  var seg = e.target.closest("textarea.sc-text-segment-input");
  if (seg) {
    if (e.key === "Enter") {
      e.stopPropagation();
      return;
    }
    if (e.key === "Backspace" && scTextBackspaceRemoveVar(seg)) {
      e.preventDefault();
      return;
    }
  }
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
  if (e.target.closest(".sc-text-var-bar .sc-text-var-chip")) e.preventDefault();
  var composerHit = e.target.closest(".sc-text-composer");
  if (composerHit && !e.target.closest("textarea.sc-text-segment-input") &&
      !e.target.closest(".sc-text-var-inline") && !e.target.closest(".sc-text-var-bar")) {
    e.preventDefault();
    scFocusTextComposerFromEvent(composerHit, e.clientX);
    return;
  }
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
  var textVarChip = e.target.closest(".sc-text-var-bar .sc-text-var-chip");
  if (textVarChip) {
    e.preventDefault();
    e.stopPropagation();
    scAppendTextVariable(Number(textVarChip.dataset.idx), textVarChip.dataset.kind, textVarChip.dataset.textVar);
    return;
  }
  var inlineVar = e.target.closest(".sc-text-var-inline");
  if (inlineVar) {
    e.preventDefault();
    e.stopPropagation();
    var blockEl = inlineVar.closest(".sc-block[data-block-idx]");
    var compEl = inlineVar.closest(".sc-text-composer");
    var blockIdx = Number((blockEl && blockEl.dataset.blockIdx) || (compEl && compEl.dataset.idx) || 0);
    var oldVar = inlineVar.dataset.varId || "";
    var vars = scSmartFieldVariables("textPart", blockIdx);
    if (!vars.length) return;
    var opts = vars.map(function (v) {
      return {value: v.id, label: v.label, isVar: true};
    });
    scOpenPop(inlineVar, opts, function (val) {
      scPushHistory();
      scReplaceTextVariableId(blockIdx, oldVar, val, false);
    }, oldVar);
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
  var ifExpand = e.target.closest("[data-if-expand]");
  if (ifExpand) {
    scIfExpandToMulti(Number(ifExpand.dataset.ifExpand));
    return;
  }
  var addIfCond = e.target.closest("[data-add-if-condition]");
  if (addIfCond) {
    scIfAddCondition(Number(addIfCond.dataset.addIfCondition));
    return;
  }
  var remIfCond = e.target.closest("[data-remove-if-condition]");
  if (remIfCond) {
    scIfRemoveCondition(Number(remIfCond.dataset.removeIfCondition), Number(remIfCond.dataset.ifCondIdx));
    return;
  }
  var repeatTimes = e.target.closest(".sc-repeat-times-pill");
  if (repeatTimes) {
    e.stopPropagation();
    scOpenRepeatStepperPop(repeatTimes, Number(repeatTimes.dataset.repeatTimes), repeatTimes.dataset.kind || "action");
    return;
  }
  var waitSeconds = e.target.closest(".sc-wait-seconds-pill");
  if (waitSeconds) {
    e.stopPropagation();
    scOpenWaitStepperPop(waitSeconds, Number(waitSeconds.dataset.waitSeconds), waitSeconds.dataset.kind || "action");
    return;
  }
  var del = e.target.closest("[data-del-action]");
  if (del) {
    scRemoveActionBlock(Number(del.dataset.delAction));
    return;
  }
  var menuItem = e.target.closest(".sc-smart-menu-item");
  if (menuItem) {
    scHandleSmartMenuPick(menuItem);
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
      !e.target.closest(".sc-pill[data-field]") && !e.target.closest(".sc-stepper-pill") &&
      !e.target.closest(".sc-smart-menu-item") &&
      !e.target.closest(".sc-text-composer") && !e.target.closest(".sc-text-var-bar") &&
      !e.target.closest("#sc-text-var-pop")) {
    scCloseAllPickers(null);
  }
  var pill = e.target.closest(".sc-pill");
  if (!pill || pill.tagName === "INPUT" || pill.classList.contains("sc-smart-trigger")) return;
  var kind = pill.dataset.kind;
  var idx = Number(pill.dataset.idx || 0);
  var condIdx = pill.dataset.condIdx != null ? Number(pill.dataset.condIdx) : null;
  var field = pill.dataset.field;
  var cur = scGetBlockRef(kind, idx, condIdx);
  var curVal = cur ? cur[field] : null;
  if (field === "enabled") curVal = cur && cur.enabled ? "on" : "off";
  if (field === "op" && cur) curVal = cur.op;
  if (field === "match" && kind === "action") {
    var ifAct = scEditing.actions[idx];
    curVal = ifAct && ifAct.match === "any" ? "any" : "all";
  }
  var opts = field === "kind" ? scFilterFieldOptions(idx) : scOptionsForField(field, idx, condIdx);
  if (!opts.length) return;
  scOpenPop(pill, opts, function (val) { scSetBlockField(kind, idx, field, val, condIdx); }, curVal);
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
  if (sc) scOpenEditor(sc, "automation");
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
