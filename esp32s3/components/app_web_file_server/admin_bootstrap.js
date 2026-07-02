async function checkAuth(){try{const r=await fetch('/api/auth/status');if(!r.ok){location.href='/';return;}await loadFiles();}catch(e){location.href='/'}}
async function logout(){try{await fetch('/api/logout',{method:'POST'})}catch(e){}location.href='/'}
async function initApp(){
  bindCoreEvents();
  await checkAuth();
  await renderBuildTime();
}
Object.assign(window,{
  actDefault,
  actDownload,
  actFlashDump,
  actReadback,
  cancelBoardConnect,
  cancelDownloadConfirm,
  changeFlashDumpPage,
  confirmDownloadConfirm,
  exportFlashDump,
  loadFiles,
  logout,
  queryFlashDumpPage,
  switchAboutSection,
  switchAdvancedFeature,
  switchPage,
  switchParamType,
  upload,
});
initApp();
