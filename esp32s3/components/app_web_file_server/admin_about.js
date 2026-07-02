function switchAboutSection(id){
  document.querySelectorAll('[data-about-tab]').forEach(btn=>btn.classList.toggle('active',btn.dataset.aboutTab===id));
  document.querySelectorAll('[data-about-section]').forEach(sec=>{sec.hidden=sec.dataset.aboutSection!==id;});
}
async function renderBuildTime(){
  const buildTime=$('appBuildTime');
  if(!buildTime)return;
  try{
    const r=await fetch('/api/app/info',{cache:'no-store'});
    const j=await r.json();
    if(r.status===401){location.href='/';return;}
    if(!r.ok||!j.ok)throw new Error((j&&j.error)||r.statusText);
    buildTime.textContent=j.buildTime||'未获取';
  }catch(e){
    buildTime.textContent='未获取';
  }
}
