// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/cosmetic_scripts.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace aerie_adblock {

namespace {

// A number from cosmetic_limits.h, as script text.
std::u16string Limit(size_t n) {
  return base::NumberToString16(n);
}

}  // namespace

std::u16string CollectClassesAndIdsScript() {
  const std::u16string max_tokens = Limit(kMaxPageTokens);
  return base::StrCat(
      {u"(function(){"
       u"var c=new Set(),d=new Set(),n=0,"
       u"all=document.querySelectorAll('[class],[id]');"
       u"for(var i=0;i<all.length&&n<",
       Limit(kMaxScannedElements),
       u";i++,n++){"
       u"var e=all[i];"
       u"if(e.classList){for(var j=0;j<e.classList.length;j++){"
       u"c.add(e.classList[j]);}}"
       u"if(typeof e.id==='string'&&e.id){d.add(e.id);}"
       u"}"
       u"return [Array.from(c).slice(0,",
       max_tokens,
       u").join(' '),"
       u"Array.from(d).slice(0,",
       max_tokens,
       u").join(' ')];"
       u"})()"});
}

std::u16string LateWatchScript() {
  // Fixed text. It reads class names and ids from inserted elements and gives
  // them to the renderer's native function; it writes nothing to the page.
  //
  // - Tokens that are not ASCII are dropped here: the engine only takes ASCII,
  //   and one bad token must not void a report.
  // - Mutation records wait in `work` and are read a bounded amount at a time,
  //   so the cost is bounded. A flood of junk delays an ad's class, and the
  //   delay is fair only within the limits: `work`, the token queues, the
  //   elements read per document and the retries are bounded, and a hostile page
  //   can exhaust them.
  // - A token leaves its queue only when `notify` returned true (the renderer
  //   sent the report), so a report that was dropped is sent again.
  // - `bud` counts characters; tokens are ASCII (see above), so a character is
  //   a byte and a report stays under kMaxLateTokenListBytes.
  return base::StrCat(
      {uR"JS((function(){
if(globalThis.__aerieLateWatch){return;}
var notify=globalThis.__aerieCosmeticNotify;
if(typeof notify!=='function'||typeof MutationObserver!=='function'||!document.documentElement){return;}
globalThis.__aerieLateWatch=true;
var kc=new Set(),ki=new Set(),qc=[],qi=[],work=[],timer=0,turn=0,fails=0,left=)JS",
       Limit(kMaxLateElementsPerDocument),
       uR"JS(;
function ok(t,k){return t.length<=)JS",
       Limit(kMaxTokenBytes),
       uR"JS(&&!/[^\x00-\x7f]/.test(t)&&!k.has(t)&&k.size<)JS",
       Limit(kMaxLateSeenTokens),
       uR"JS(;}
function add(e){
var cl=e.classList;
if(cl){for(var j=0;j<cl.length&&j<)JS",
       Limit(kMaxClassesPerElement),
       uR"JS(;j++){var c=cl[j];if(ok(c,kc)){kc.add(c);qc.push(c);}}}
var d=e.id;
if(typeof d==='string'&&d&&ok(d,ki)){ki.add(d);qi.push(d);}
}
function take(q){var bud=)JS",
       Limit(kMaxLateTokenListBytes),
       uR"JS(,n=0;while(n<q.length&&q[n].length+1<=bud){bud-=q[n].length+1;n++;}return n;}
function stop(){mo.disconnect();work.length=0;qc.length=0;qi.length=0;}
function flush(){
timer=0;
if(!qc.length&&!qi.length){return;}
var nc=take(qc),ni=take(qi),sent=false;
try{sent=notify(qc.slice(0,nc).join(' '),qi.slice(0,ni).join(' '))===true;}catch(e){}
if(sent){fails=0;qc.splice(0,nc);qi.splice(0,ni);}
else if(++fails>)JS",
       Limit(kMaxLateFlushRetries),
       uR"JS(){stop();return;}
if(qc.length||qi.length){timer=setTimeout(flush,sent?)JS",
       Limit(kLateFlushDelayMs),
       uR"JS(:)JS",
       Limit(kLateRetryDelayMs),
       uR"JS();}
}
function drain(){
if(turn){clearTimeout(turn);turn=0;}
var room=)JS",
       Limit(kMaxElementsPerMutationBatch),
       uR"JS(,recs=)JS",
       Limit(kMaxMutationRecordsPerBatch),
       uR"JS(;
while(work.length&&room>0&&recs>0&&left>0){
var it=work[0];
if(!it.w){
add(it.n);room--;left--;recs--;
if(it.a||!it.n.firstElementChild){work.shift();continue;}
it.w=document.createTreeWalker(it.n,NodeFilter.SHOW_ELEMENT);
}
var x;
while(room>0&&left>0&&(x=it.w.nextNode())){add(x);room--;left--;}
if(room>0&&left>0){work.shift();}
}
if(left<=0){stop();return;}
if(work.length){turn=setTimeout(drain,)JS",
       Limit(kLateFlushDelayMs),
       uR"JS();}
if((qc.length||qi.length)&&!timer){timer=setTimeout(flush,)JS",
       Limit(kLateFlushDelayMs),
       uR"JS();}
}
var mo=new MutationObserver(function(recs){
for(var k=0;k<recs.length&&work.length<)JS",
       Limit(kMaxLateBacklogItems),
       uR"JS(;k++){
var r=recs[k];
if(r.type==='attributes'){if(r.target.nodeType===1){work.push({n:r.target,a:true,w:null});}continue;}
var a=r.addedNodes;
for(var m=0;m<a.length&&work.length<)JS",
       Limit(kMaxLateBacklogItems),
       uR"JS(;m++){if(a[m].nodeType===1){work.push({n:a[m],a:false,w:null});}}
}
drain();
});
mo.observe(document.documentElement,{childList:true,subtree:true,attributes:true,attributeFilter:['class','id']});
})())JS"});
}

std::optional<PageTokens> ParsePageTokens(const base::Value& result) {
  if (!result.is_list()) {
    return std::nullopt;
  }
  const base::ListValue& list = result.GetList();
  if (list.size() != 2 || !list[0].is_string() || !list[1].is_string()) {
    return std::nullopt;
  }
  const std::string& classes = list[0].GetString();
  const std::string& ids = list[1].GetString();
  if (classes.size() > kMaxTokenListBytes || ids.size() > kMaxTokenListBytes) {
    return std::nullopt;
  }
  return PageTokens{classes, ids};
}

}  // namespace aerie_adblock
