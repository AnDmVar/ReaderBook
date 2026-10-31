#pragma once
#include <QString>
namespace SitePresentation { inline QString injection(){return QString::fromUtf8(R"JS((function(){
  if(window.__pbPresentationInstalled)return;window.__pbPresentationInstalled=true;
  if(location.pathname.indexOf('/static/reader/')===0)return;
  var style=document.createElement('style');style.id='pb-site-presentation';
  style.textContent=
    '[data-testid="cookieAcceptPopup"]{position:fixed!important;left:12px!important;right:12px!important;bottom:12px!important;transform:none!important;width:auto!important;min-width:0!important;max-width:calc(100vw - 24px)!important;max-height:calc(100vh - 24px)!important;box-sizing:border-box!important;display:flex!important;flex-direction:column!important;overflow-y:auto!important;padding:16px!important;z-index:2147483000!important;}' +
    '[data-testid="cookieAcceptPopup"] p{white-space:normal!important;overflow-wrap:break-word!important;word-wrap:break-word!important;min-width:0!important;max-width:100%!important;margin:0 0 12px!important;}' +
    '[data-testid="cookieAcceptPopup__accept"]{width:100%!important;max-width:100%!important;min-width:0!important;box-sizing:border-box!important;flex-shrink:0!important;}' +
    '[data-pb-cover]{max-width:100%!important;height:auto!important;object-fit:contain!important;}[data-pb-cover-frame]>[data-pb-cover]{position:absolute!important;inset:0!important;left:0!important;top:0!important;width:100%!important;height:100%!important;max-width:100%!important;object-fit:contain!important;object-position:center!important;display:block!important;margin:0!important;}' +
    '[data-pb-cover-frame]{position:relative!important;height:var(--pb-cover-height)!important;min-height:0!important;max-width:100%!important;align-self:flex-start!important;padding:0!important;overflow:hidden!important;}';
  (document.head||document.documentElement).appendChild(style);
  function apply(){
    document.querySelectorAll('img[data-testid="adaptiveCover__img"],img[src*="/pub/c/cover_"],img[srcset*="/pub/c/cover_"]').forEach(function(img){
      if(img.getAttribute('data-state')==='error'){
        img.removeAttribute('data-pb-cover');
        if(img.parentElement)img.parentElement.removeAttribute('data-pb-cover-frame');
        return;
      }
      if(!img.hasAttribute('data-pb-cover'))img.setAttribute('data-pb-cover','');
      var parent=img.parentElement;
      if(parent&&parent.classList.contains('uik-container-oVrQr5')){
        if(!parent.hasAttribute('data-pb-cover-frame'))parent.setAttribute('data-pb-cover-frame','');
        var w=parent.getBoundingClientRect().width,ratio=img.naturalWidth>0&&img.naturalHeight>0?img.naturalWidth/img.naturalHeight:2/3;
        if(w>0)parent.style.setProperty('--pb-cover-height',Math.round(w/ratio)+'px');
        if(!img.__pbCoverLoad){img.__pbCoverLoad=true;img.addEventListener('load',apply);}
      }
    });
  }
  var pending=false;
  new MutationObserver(function(){if(pending)return;pending=true;requestAnimationFrame(function(){pending=false;apply();});}).observe(document.documentElement,{childList:true,subtree:true,attributes:true,attributeFilter:['src','srcset','data-state']});
  window.addEventListener('resize',apply);apply();
})();
)JS");} }
