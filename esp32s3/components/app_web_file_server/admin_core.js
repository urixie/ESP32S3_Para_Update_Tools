const $=id=>document.getElementById(id);
const PARAM_TIME_BASE_NS=50;
const PARAM_MAX_BOARD_VALUE=65535;
const PARAM_MAX_NS=PARAM_MAX_BOARD_VALUE*PARAM_TIME_BASE_NS;
const FLASH_DUMP_TOTAL_BYTES=512*1024;
const FLASH_DUMP_PAGE_BYTES=1024;
const FLASH_DUMP_ROW_BYTES=74;
const FLASH_DUMP_PAGE_COUNT=FLASH_DUMP_TOTAL_BYTES/FLASH_DUMP_PAGE_BYTES;
const FLASH_DUMP_ROWS_PER_PAGE=Math.ceil(FLASH_DUMP_PAGE_BYTES/FLASH_DUMP_ROW_BYTES);
const FLASH_DUMP_CELL_WIDTH=38;
const FLASH_DUMP_ROW_LABEL_WIDTH=64;
const FLASH_DUMP_HEADER_HEIGHT=28;
const FLASH_DUMP_ROW_HEIGHT=24;
const HEX_TABLE=Array.from({length:256},(_,i)=>i.toString(16).toUpperCase().padStart(2,'0'));
const paramCounts={}; // path -> 已解析得到的参数数量
const boardParamStates={}; // path -> { parameters: [], currentParamType: 'control', parsed: false }
let allFiles=[],binFiles=[],selected=null,uploading=false,paramBusy=false,parsedParams=[],currentParamType='control',currentPage='params';
let toastTimer=null;
let boardConnectCanceled=false;
let downloadConfirmResolver=null;
let flashDumpBusy=false,flashDumpBuffer=null,flashDumpBytes=null,flashDumpOpId=0;
let flashDumpPageIndex=0,flashDumpScrollRaf=0;
let currentAdvancedFeature='flashRead';
function bindCoreEvents(){
  const fileInput=$('file');
  const fileName=$('fileName');
  if(fileInput&&fileName){
    fileInput.addEventListener('change',()=>{fileName.textContent=fileInput.files[0]?'已选择：'+fileInput.files[0].name:'未选择文件';});
  }
  const flashDumpView=$('flashDumpView');
  if(flashDumpView){
    flashDumpView.addEventListener('scroll',()=>scheduleFlashDumpStickySync(),{passive:true});
  }
}
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function setMsg(s,ok=false){$('msg').className='msg '+(ok?'ok':'');$('msg').textContent=s||''}
function showToast(s,duration=1000){
  const toast=$('toast');
  if(!toast)return;
  if(toastTimer)clearTimeout(toastTimer);
  toast.textContent=s;
  toast.classList.add('show');
  toastTimer=setTimeout(()=>{toast.classList.remove('show');},duration);
}
async function readErr(r){try{const j=await r.clone().json();return j.error||r.statusText}catch(e){return await r.text()||r.statusText}}
function xhrErrorText(x){try{const j=JSON.parse(x.responseText||'{}');return j.error||x.statusText}catch(e){return x.responseText||x.statusText||'连接失败'}}
function setBusy(b){uploading=b;$('uploadBtn').disabled=b;$('refreshBtn').disabled=b}
function updateParamButtons(){
  const disabled=paramBusy||flashDumpBusy||!selected;
  $('btnDefault').disabled=disabled;
  $('btnReadback').disabled=disabled;
  $('btnDownload').disabled=disabled;
}
function updateFlashDumpButtons(){
  const hasDump=!!flashDumpBuffer;
  const canQuery=hasDump&&!flashDumpBusy;
  $('btnFlashDumpStart').disabled=flashDumpBusy||paramBusy;
  $('btnFlashDumpExport').disabled=flashDumpBusy||!hasDump;
  $('flashDumpPageInput').disabled=!canQuery;
  $('btnFlashDumpQuery').disabled=!canQuery;
  $('btnFlashDumpPrev').disabled=!canQuery||flashDumpPageIndex<=0;
  $('btnFlashDumpNext').disabled=!canQuery||flashDumpPageIndex>=FLASH_DUMP_PAGE_COUNT-1;
}
function setParamBusy(b){
  paramBusy=b;
  updateParamButtons();
  updateFlashDumpButtons();
}
function setFlashDumpBusy(b){
  flashDumpBusy=b;
  updateParamButtons();
  updateFlashDumpButtons();
}
function openBoardConnectDialog(actionText){
  $('connectAction').textContent=actionText||'正在和参数板卡握手';
  $('connectDialog').classList.remove('hidden');
}
function closeBoardConnectDialog(){
  $('connectDialog').classList.add('hidden');
}
function cancelBoardConnect(){
  boardConnectCanceled=true;
  closeBoardConnectDialog();
  showToast('已取消连接');
  try{
    fetch('/api/param/connect/cancel',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'',keepalive:true}).catch(()=>{});
  }catch(e){}
}
function sleep(ms){return new Promise(resolve=>setTimeout(resolve,ms));}
async function getParamBoardStatus(opId,path){
  let url='/api/param/status?id='+encodeURIComponent(opId);
  if(path)url+='&path='+encodeURIComponent(path);
  const r=await fetch(url,{cache:'no-store'});
  let j=null;
  try{j=await r.json();}catch(e){j={ok:false,error:r.statusText};}
  if(r.status===401){location.href='/';return null;}
  if(!r.ok||!j.ok)throw new Error((j&&j.error)||r.statusText);
  return j;
}
async function runParamBoardOperation(actionText,url,fields){
  boardConnectCanceled=false;
  openBoardConnectDialog(actionText);
  try{
    const started=await postForm(url,fields);
    if(!started||!started.opId)return started;
    const opId=started.opId;
    while(!boardConnectCanceled){
      const status=await getParamBoardStatus(opId,fields&&fields.path);
      if(!status)return null;
      if(boardConnectCanceled)return null;
      if(status.message)$('connectAction').textContent=status.message;
      if(status.state==='done')return status;
      if(status.state==='canceled')return null;
      if(status.state==='failed')throw new Error(status.error||status.message||'参数板卡操作失败');
      if(status.state!=='running')throw new Error(status.error||status.message||'参数板卡状态异常');
      await sleep(150);
      if(boardConnectCanceled){
        try{
          await fetch('/api/param/connect/cancel',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'',keepalive:true});
        }catch(e){}
        return null;
      }
    }
    return null;
  }catch(e){
    if(boardConnectCanceled)return null;
    throw e;
  }finally{
    closeBoardConnectDialog();
    boardConnectCanceled=false;
  }
}
async function postForm(url,fields,options={}){
  const body=new URLSearchParams(fields);
  const fetchOptions={method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body};
  if(options.signal)fetchOptions.signal=options.signal;
  const r=await fetch(url,fetchOptions);
  let j=null;
  try{j=await r.json();}catch(e){j={ok:false,error:r.statusText};}
  if(r.status===401){location.href='/';return null;}
  if(!r.ok||!j.ok)throw new Error((j&&j.error)||r.statusText);
  return j;
}
