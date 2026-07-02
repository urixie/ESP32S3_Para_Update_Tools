function setFlashDumpMessage(message,isError=false){
  const status=$('flashDumpStatus');
  status.className='badge flash-dump-status'+(isError?' err-state':'');
  status.textContent=message||'';
}
function setFlashDumpProgress(bytes,total,message){
  const safeTotal=total||FLASH_DUMP_TOTAL_BYTES;
  const safeBytes=Math.max(0,Math.min(bytes||0,safeTotal));
  const percent=safeTotal?Math.floor(safeBytes*100/safeTotal):0;
  $('flashDumpProgressBar').style.width=percent+'%';
  $('flashDumpProgressText').textContent=percent+'%';
  if(message)setFlashDumpMessage(message);
}
function resetFlashDumpView(){
  flashDumpBuffer=null;
  flashDumpBytes=null;
  flashDumpPageIndex=0;
  if(flashDumpScrollRaf){
    cancelAnimationFrame(flashDumpScrollRaf);
    flashDumpScrollRaf=0;
  }
  const view=$('flashDumpView');
  view.scrollLeft=0;
  view.scrollTop=0;
  $('flashDumpPageInput').value='0';
  setFlashDumpPageInfo(0);
  $('flashDumpCanvas').style.width='0px';
  $('flashDumpCanvas').style.height='0px';
  $('flashDumpCols').innerHTML='';
  $('flashDumpRowLabels').innerHTML='';
  $('flashDumpCells').innerHTML='';
  $('flashDumpView').classList.add('hidden');
  $('flashDumpEmpty').classList.remove('hidden');
  setFlashDumpProgress(0,FLASH_DUMP_TOTAL_BYTES,'等待 flash回读');
  updateFlashDumpButtons();
}
function clampFlashDumpPage(value){
  let page=Number.parseInt(value,10);
  if(!Number.isFinite(page))page=0;
  if(page<0)page=0;
  if(page>=FLASH_DUMP_PAGE_COUNT)page=FLASH_DUMP_PAGE_COUNT-1;
  return page;
}
function setFlashDumpPageInfo(page){
  const start=page*FLASH_DUMP_PAGE_BYTES;
  const end=start+FLASH_DUMP_PAGE_BYTES-1;
  $('flashDumpPageInfo').textContent='页 '+page+' / '+(FLASH_DUMP_PAGE_COUNT-1)+'，起始地址 '+start+'，范围 '+start+' - '+end;
}
function setFlashDumpStickyTransform(view){
  const x=view.scrollLeft;
  const y=view.scrollTop;
  $('flashDumpCorner').style.transform='translate('+x+'px,'+y+'px)';
  $('flashDumpCols').style.transform='translateY('+y+'px)';
  $('flashDumpRowLabels').style.transform='translateX('+x+'px)';
}
function setFlashDumpCanvasSize(){
  const width=FLASH_DUMP_ROW_LABEL_WIDTH+FLASH_DUMP_ROW_BYTES*FLASH_DUMP_CELL_WIDTH;
  const height=FLASH_DUMP_HEADER_HEIGHT+FLASH_DUMP_ROWS_PER_PAGE*FLASH_DUMP_ROW_HEIGHT;
  const canvas=$('flashDumpCanvas');
  canvas.style.width=width+'px';
  canvas.style.height=height+'px';
  $('flashDumpCorner').style.left='0px';
  $('flashDumpCorner').style.top='0px';
  $('flashDumpCorner').style.width=FLASH_DUMP_ROW_LABEL_WIDTH+'px';
  $('flashDumpCorner').style.height=FLASH_DUMP_HEADER_HEIGHT+'px';
  $('flashDumpCols').style.width=width+'px';
  $('flashDumpCols').style.height=FLASH_DUMP_HEADER_HEIGHT+'px';
  $('flashDumpRowLabels').style.width=FLASH_DUMP_ROW_LABEL_WIDTH+'px';
  $('flashDumpRowLabels').style.height=height+'px';
}
function scheduleFlashDumpStickySync(){
  if(flashDumpScrollRaf)return;
  flashDumpScrollRaf=requestAnimationFrame(()=>{
    flashDumpScrollRaf=0;
    setFlashDumpStickyTransform($('flashDumpView'));
  });
}
function renderFlashDumpPage(page){
  if(!flashDumpBytes)return;
  const safePage=clampFlashDumpPage(page);
  flashDumpPageIndex=safePage;
  $('flashDumpPageInput').value=String(safePage);
  setFlashDumpPageInfo(safePage);
  setFlashDumpCanvasSize();
  const view=$('flashDumpView');
  view.scrollLeft=0;
  view.scrollTop=0;
  setFlashDumpStickyTransform(view);

  const cols=[];
  for(let col=0;col<FLASH_DUMP_ROW_BYTES;col++){
    cols.push('<div class="flash-dump-col" style="left:'+(FLASH_DUMP_ROW_LABEL_WIDTH+col*FLASH_DUMP_CELL_WIDTH)+'px;top:0;width:'+FLASH_DUMP_CELL_WIDTH+'px;height:'+FLASH_DUMP_HEADER_HEIGHT+'px">'+col+'</div>');
  }
  $('flashDumpCols').innerHTML=cols.join('');

  const labels=[];
  const cells=[];
  const pageOffset=safePage*FLASH_DUMP_PAGE_BYTES;
  for(let row=0;row<FLASH_DUMP_ROWS_PER_PAGE;row++){
    const top=FLASH_DUMP_HEADER_HEIGHT+row*FLASH_DUMP_ROW_HEIGHT;
    labels.push('<div class="flash-dump-row-label" style="left:0;top:'+top+'px;width:'+FLASH_DUMP_ROW_LABEL_WIDTH+'px;height:'+FLASH_DUMP_ROW_HEIGHT+'px">'+row+'</div>');
    const rowOffset=pageOffset+row*FLASH_DUMP_ROW_BYTES;
    const rowEnd=Math.min(FLASH_DUMP_ROW_BYTES,flashDumpBytes.length-rowOffset);
    for(let col=0;col<rowEnd;col++){
      const left=FLASH_DUMP_ROW_LABEL_WIDTH+col*FLASH_DUMP_CELL_WIDTH;
      cells.push('<div class="flash-dump-cell" style="left:'+left+'px;top:'+top+'px;width:'+FLASH_DUMP_CELL_WIDTH+'px;height:'+FLASH_DUMP_ROW_HEIGHT+'px">'+HEX_TABLE[flashDumpBytes[rowOffset+col]]+'</div>');
    }
  }
  $('flashDumpRowLabels').innerHTML=labels.join('');
  $('flashDumpCells').innerHTML=cells.join('');
  updateFlashDumpButtons();
}
function renderFlashDumpData(buffer){
  flashDumpBuffer=buffer;
  flashDumpBytes=new Uint8Array(buffer);
  $('flashDumpEmpty').classList.add('hidden');
  $('flashDumpView').classList.remove('hidden');
  renderFlashDumpPage(0);
}
function queryFlashDumpPage(){
  if(!flashDumpBytes)return;
  renderFlashDumpPage($('flashDumpPageInput').value);
}
function changeFlashDumpPage(delta){
  if(!flashDumpBytes)return;
  renderFlashDumpPage(flashDumpPageIndex+delta);
}
function switchAdvancedFeature(feature){
  currentAdvancedFeature=feature==='flashDownload'?'flashDownload':'flashRead';
  const isRead=currentAdvancedFeature==='flashRead';
  $('tabFlashRead').classList.toggle('active',isRead);
  $('tabFlashDownload').classList.toggle('active',!isRead);
  $('flashReadPanel').classList.toggle('hidden',!isRead);
  $('flashDownloadPanel').classList.toggle('hidden',isRead);
  if(isRead&&flashDumpBuffer){
    scheduleFlashDumpStickySync();
  }
}
async function getFlashDumpStatus(opId){
  const r=await fetch('/api/flash-dump/status?id='+encodeURIComponent(opId),{cache:'no-store'});
  let j=null;
  try{j=await r.json();}catch(e){j={ok:false,error:r.statusText};}
  if(r.status===401){location.href='/';return null;}
  if(!r.ok||!j.ok)throw new Error((j&&j.error)||r.statusText);
  return j;
}
async function fetchFlashDumpData(){
  const r=await fetch('/api/flash-dump/data',{cache:'no-store'});
  if(r.status===401){location.href='/';return null;}
  if(!r.ok)throw new Error(await readErr(r));
  const buffer=await r.arrayBuffer();
  if(buffer.byteLength!==FLASH_DUMP_TOTAL_BYTES){
    throw new Error('flash回读 数据长度异常：'+buffer.byteLength+' 字节');
  }
  return buffer;
}
async function actFlashDump(){
  if(flashDumpBusy)return;
  resetFlashDumpView();
  setFlashDumpBusy(true);
  try{
    showToast('正在启动 flash回读...',1200);
    const started=await postForm('/api/flash-dump/start',{});
    if(!started||!started.opId)return;
    flashDumpOpId=started.opId;
    while(true){
      const status=await getFlashDumpStatus(flashDumpOpId);
      if(!status)return;
      setFlashDumpProgress(status.bytesRead,status.totalBytes,(status.message||'正在读取 flash回读').replace(/flash dump/g,'flash回读'));
      if(status.state==='done')break;
      if(status.state==='canceled'){
        setFlashDumpMessage((status.message||'flash回读 已取消').replace(/flash dump/g,'flash回读'),true);
        return;
      }
      if(status.state==='failed')throw new Error((status.error||status.message||'flash回读 失败').replace(/flash dump/g,'flash回读'));
      if(status.state!=='running')throw new Error((status.error||status.message||'flash回读 状态异常').replace(/flash dump/g,'flash回读'));
      await sleep(350);
    }
    setFlashDumpMessage('正在获取 flash回读 数据');
    const buffer=await fetchFlashDumpData();
    if(!buffer)return;
    flashDumpBuffer=buffer;
    renderFlashDumpData(buffer);
    setFlashDumpProgress(FLASH_DUMP_TOTAL_BYTES,FLASH_DUMP_TOTAL_BYTES,'flash回读 完成');
    showToast('flash回读 完成');
  }catch(e){
    setFlashDumpMessage('flash回读 失败：'+e.message,true);
    showToast('flash回读 失败：'+e.message,1800);
  }finally{
    setFlashDumpBusy(false);
  }
}
function exportFlashDump(){
  if(!flashDumpBuffer)return;
  const blob=new Blob([flashDumpBuffer],{type:'application/octet-stream'});
  const url=URL.createObjectURL(blob);
  const a=document.createElement('a');
  a.href=url;
  a.download='flash_dump_512kb.bin';
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(()=>URL.revokeObjectURL(url),1000);
}
