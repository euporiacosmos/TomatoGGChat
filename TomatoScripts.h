#pragma once
// ─────────────────────────────────────────────────────────────
// Tomato.gg 페이지에 주입하는 JavaScript 모음.
// ★ 사이트 DOM 이 바뀌어 수집이 깨지면 이 파일만 고치면 됩니다.
//   (앱에서 /dump 를 입력하면 첫 두 전투 행의 outerHTML 이 파일로 저장됩니다.)
// ─────────────────────────────────────────────────────────────
#include <string>

namespace TomatoJs {

// 페이지 전체 보이는 텍스트
inline const wchar_t* PageText =
    L"(function(){ return document.body ? document.body.innerText : ''; })()";

// 사이트가 지금 보여주는 지표 모드: 'WNX' / 'WN8' / ''
inline const wchar_t* SiteMode = LR"JS(
(function(){
  var all = document.querySelectorAll('div,span,p,button,a');
  for (var i = 0; i < all.length; i++) {
    var t = (all[i].textContent || '').trim();
    if (t.length > 30) continue;
    var m = /^Recent\s*(WNX|WN8)/i.exec(t);
    if (m) return m[1].toUpperCase();
  }
  return '';
})()
)JS";

// Battles 패널 공통 함수들
inline const wchar_t* BattlesCommon = LR"JS(
function tgcFindHeader() {
  var cand = document.querySelectorAll('div,span,h1,h2,h3,h4,h5,p,button,a');
  for (var i = 0; i < cand.length; i++) {
    var e = cand[i];
    if ((e.textContent || '').trim() !== 'Battles') continue;
    var r = e.getBoundingClientRect();
    if (r.height <= 0 || r.height > 60) continue;
    var a = e;
    for (var d = 0; d < 5 && a; d++, a = a.parentElement) {
      var t = a.innerText || '';
      if (t.length > 300) break;
      if (t.indexOf('Sessions') >= 0 && t.indexOf('Filters') >= 0) return a;
    }
  }
  return null;
}
function tgcRowOf(el, minW) {
  var p = el;
  for (var d = 0; d < 14 && p; d++, p = p.parentElement) {
    var r = p.getBoundingClientRect();
    if (r.height > 180) return null;
    if (r.width >= minW && r.height >= 32 && p.querySelector('img')) return p;
  }
  return null;
}
function tgcParseColor(c) {
  var m = /rgba?\(\s*([\d.]+)[,\s]+([\d.]+)[,\s]+([\d.]+)(?:[,\s/]+([\d.]+))?\s*\)/.exec(c || '');
  return m ? [+m[1], +m[2], +m[3], m[4] === undefined ? 1 : +m[4]] : null;
}
// Tomato.gg 의 BattleRow DOM 에서 값을 의미 단위로 읽는다. (클래스명은 styled-components 해시가 붙으므로 접두어만 사용)
function tgcRowInfo(row) {
  function txt(sel) { var e = row.querySelector(sel); return e ? (e.textContent || '').trim() : ''; }
  var info = {};
  var top = row.querySelector('[result]');                              // <div result="win|loss">
  info.res = top ? (top.getAttribute('result') || '') : '';
  var spans = row.querySelectorAll('[class*="ImageSection__MapName"] span');
  info.map = spans.length > 0 ? (spans[0].textContent || '').trim() : '';
  info.ago = spans.length > 1 ? (spans[1].textContent || '').trim() : '';
  info.tier = txt('[class*="tableComponents__Tier"]');                  // 로마 숫자 (X)
  info.tank = txt('[class*="ImageSection__TankName"]');
  var ci = row.querySelector('img[src*="/classIcons/"]');               // 차량 종류 아이콘 (SPG 이면 자주포)
  info.cls = ci ? (ci.getAttribute('alt') || '') : '';
  var src = ci ? (ci.getAttribute('src') || '') : '';
  info.clsSrc = src.split('/').pop().replace(/\.[a-z0-9]+$/i, '');
  var data = {};                                                        // 아이콘 alt("damage icon" 등) → 값
  var ds = row.querySelectorAll('[class*="DataSection__Datum_"]');
  for (var i = 0; i < ds.length; i++) {
    var im = ds[i].querySelector('img');
    var key = im ? (im.getAttribute('alt') || '').replace(/\s*icon\s*$/i, '').trim().toLowerCase() : '';
    if (key) data[key] = (ds[i].textContent || '').trim();
  }
  info.data = data;
  var lc = row.querySelectorAll('[class*="DataSection__LastCol"]');     // "WNX 218" 칸
  for (var k = 0; k < lc.length; k++) {
    var m = /^(WNX|WN8)\s*(\d[\d,]*)/i.exec((lc[k].textContent || '').replace(/\s+/g, ' ').trim());
    if (m) { info.label = m[1].toUpperCase(); info.rating = m[2]; }
  }
  return info;
}
function tgcWin(row) {
  var els = [row];
  for (var i = 0; i < row.children.length && i < 4; i++) els.push(row.children[i]);
  for (var k = 0; k < els.length; k++) {
    var cs = getComputedStyle(els[k]);
    var cols = [cs.backgroundColor, cs.borderLeftColor];
    for (var c = 0; c < cols.length; c++) {
      var m = tgcParseColor(cols[c]);
      if (!m || m[3] < 0.05) continue;
      if (m[1] > m[0] + 12 && m[1] >= m[2]) return true;    // 초록 계열 = 승
      if (m[0] > m[1] + 12 && m[0] >= m[2]) return false;   // 빨강 계열 = 패
    }
  }
  return null;
}
)JS";

// Battles 목록 읽기: 결과는 JSON 객체 { ok, header, mode, rows:[{lines,alts,win,top}], html:[...] }
inline std::wstring ReadBattles()
{
    std::wstring js = L"(function(){";
    js += BattlesCommon;
    js += LR"JS(
var res = { ok: false, header: false, mode: null, via: '', rows: [], html: [] };
var chosen = [];
var found = document.querySelectorAll('[class*="BattleRow__BattleRowContainer"]');   // 최신순(DOM 순서)
if (found.length) {
  res.header = true;
  res.via = 'BattleRow';
  chosen = Array.prototype.slice.call(found);
} else {
  // 예비 경로: 클래스명이 바뀐 경우 'Battles' 제목 + WNX 라벨로 행을 추정
var hdr = tgcFindHeader();
  if (!hdr) return res;
  res.header = true;
  var hr = hdr.getBoundingClientRect();
  var panelTop = hr.bottom + window.scrollY;
  var minW = Math.max(300, hr.width * 0.8);
  
  var rows = [];
  var leaves = document.querySelectorAll('div,span,a,b,strong,p');
  for (var i = 0; i < leaves.length; i++) {
    var e = leaves[i];
    if (e.children.length > 0) continue;
    var t = (e.textContent || '').trim();
    if (t !== 'WNX' && t !== 'WN8') continue;
    var er = e.getBoundingClientRect();
    if (er.height <= 0) continue;
    if (er.top + window.scrollY < panelTop) continue;
    var row = tgcRowOf(e, minW);
    if (row && rows.indexOf(row) < 0) { rows.push(row); if (!res.mode) res.mode = t; }
  }
  
  // 같은 부모를 공유하는 행이 가장 많은 그룹 = 전투 목록 (요약 블록 등 제외)
  var groups = [];
  rows.forEach(function (r) {
    var g = null;
    for (var k = 0; k < groups.length; k++) if (groups[k].parent === r.parentElement) g = groups[k];
    if (!g) { g = { parent: r.parentElement, rows: [] }; groups.push(g); }
    g.rows.push(r);
  });
  groups.sort(function (a, b) { return b.rows.length - a.rows.length; });
  var chosen = (groups.length && groups[0].rows.length >= 2) ? groups[0].rows : rows;
  chosen.sort(function (a, b) { return a.getBoundingClientRect().top - b.getBoundingClientRect().top; });
  
  }

res.rows = chosen.map(function (r) {
  var info = tgcRowInfo(r);
  info.lines = (r.innerText || r.textContent || '').split('\n').map(function (s) { return s.trim(); }).filter(Boolean);
  info.win = tgcWin(r);
  info.top = Math.round(r.getBoundingClientRect().top + window.scrollY);
  return info;
});
for (var q = 0; q < res.rows.length && !res.mode; q++) if (res.rows[q].label) res.mode = res.rows[q].label;
res.html = chosen.slice(0, 2).map(function (r) { return (r.outerHTML || '').slice(0, 12000); });
res.ok = true;
return res;
)JS";
    js += L"})()";
    return js;
}

// 지연 로딩 유도: Battles 패널을 잠깐 화면 중앙으로 스크롤했다가 원위치
inline std::wstring NudgeBattles()
{
    std::wstring js = L"(function(){";
    js += BattlesCommon;
    js += LR"JS(
var h = tgcFindHeader();
if (!h) return 'noheader';
var y = window.scrollY;
h.scrollIntoView({ block: 'center' });
setTimeout(function () { window.scrollTo(0, y); }, 900);
return 'nudged';
)JS";
    js += L"})()";
    return js;
}

} // namespace TomatoJs
