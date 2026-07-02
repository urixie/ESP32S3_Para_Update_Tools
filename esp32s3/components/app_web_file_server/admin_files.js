function isBin(f){return !f.is_dir&&(f.is_param_bin||/\.bin$/i.test(f.name||''));}
function boardLabel(f){return f.boardName||f.displayName||'未识别板卡配置'}
function normParamType(p){return String(p.paramType||'').toLowerCase();}
function currentBoardKey(){
  return selected&&selected.path?selected.path:null;
}
function getCurrentBoardState(){
  const key=currentBoardKey();
  if(!key)return null;
  if(!boardParamStates[key]){
    boardParamStates[key]={parameters:[],currentParamType:'control',parsed:false};
  }
  return boardParamStates[key];
}
function getCurrentParams(){
  const state=getCurrentBoardState();
  return state?state.parameters:[];
}
function renderFiles(){
  $('fileCount').textContent=binFiles.length;
  $('fileTable').classList.toggle('hidden',binFiles.length===0);
  const half=Math.ceil(binFiles.length/2);
  const rows=[];
  for(let row=0;row<half;row++){
    const cells=[binFiles[row],binFiles[row+half]].map(f=>{
      if(!f)return '<td class="name file-name-cell file-empty-cell"></td><td class="file-action-cell file-empty-cell"></td>';
      const label=boardLabel(f);
      return '<td class="name file-name-cell" title="'+esc(label)+'">'+esc(label)+'</td>'
        +'<td class="file-action-cell"><div class="file-actions"><button class="danger file-del" data-path="'+esc(f.path)+'">删除</button></div></td>';
    }).join('');
    rows.push('<tr>'+cells+'</tr>');
  }
  $('fileRows').innerHTML=rows.join('');
  document.querySelectorAll('.file-del').forEach(b=>b.onclick=()=>deleteFileByPath(b.dataset.path));
}
function renderBins(){
  binFiles=allFiles.filter(isBin).sort((a,b)=>boardLabel(a).localeCompare(boardLabel(b),'zh-CN'));
  $('binCount').textContent=binFiles.length;
  $('binEmpty').classList.toggle('hidden',binFiles.length>0);
  $('binList').innerHTML=binFiles.map(f=>{
    const isActive=!!(selected&&selected.path===f.path);
    const count=paramCounts[f.path];
    const countText=count!=null?'共 '+count+' 项参数':'尚未解析';
    return '<div class="bin-row">'
      +'<button class="bin'+(isActive?' active':'')+'" data-path="'+esc(f.path)+'" title="'+esc(boardLabel(f))+'">'
      +'<span class="bin-name">'+esc(boardLabel(f))+'</span>'
      +'<span class="bin-count">'+esc(countText)+'</span>'
      +'<span class="bin-hint">点击查看参数</span>'
      +'</button></div>';
  }).join('');
  document.querySelectorAll('.bin').forEach(b=>b.onclick=()=>selectBin(b.dataset.path,true));
}
function selectBin(path,parseNow=true){
  selected=binFiles.find(f=>f.path===path)||null;
  renderBins();
  if(!selected)return;
  $('title').textContent=boardLabel(selected);
  $('hint').textContent='正在解析板卡配置...';
  if(!paramBusy)setParamBusy(false);
  const state=getCurrentBoardState();
  if(state&&state.parsed){
    currentParamType=state.currentParamType||'control';
    renderParamTable();
    $('result').classList.remove('hidden');
    $('parseErr').classList.add('hidden');
    $('hint').textContent='';
    return;
  }
  if(parseNow)parseSelected();
}
async function loadFiles(){
  if(uploading){setMsg('文件正在上传，请稍后刷新');return;}
  setMsg('正在加载板卡列表...');
  try{
    const r=await fetch('/files');
    const j=await r.json();
    if(r.status===401){location.href='/';return;}
    if(!r.ok)throw new Error(j.error||r.statusText);
    allFiles=(j.files||[]).filter(f=>f.name!=='System Volume Information');
    binFiles=allFiles.filter(isBin).sort((a,b)=>boardLabel(a).localeCompare(boardLabel(b),'zh-CN'));
    if(selected){const still=binFiles.find(f=>f.path===selected.path);selected=still||null;}
    renderFiles();renderBins();
    setMsg('板卡列表已更新，共 '+binFiles.length+' 个板卡配置',true);
    if(currentPage==='params'&&!selected&&binFiles.length>0)selectBin(binFiles[0].path,true);
  }catch(e){setMsg('加载失败：'+e.message)}
}
function upload(){
  if(uploading){setMsg('文件正在上传，请稍候');return;}
  const f=$('file').files[0];
  if(!f){setMsg('请先选择一个板卡配置文件');return;}
  if(!/\.bin$/i.test(f.name)){setMsg('请上传 .bin 板卡配置文件');return;}
  setBusy(true);setMsg('准备上传...');
  const x=new XMLHttpRequest();
  x.open('POST','/upload?filename='+encodeURIComponent(f.name),true);
  x.upload.onprogress=e=>{setMsg(e.lengthComputable?'正在上传 '+Math.floor(e.loaded*100/e.total)+'%':'正在上传文件...')};
  x.onload=()=>{setBusy(false);if(x.status===401){location.href='/';return;}if(x.status<200||x.status>=300){setMsg('上传失败：'+xhrErrorText(x));return;}setMsg('上传完成，正在刷新板卡名称...',true);$('file').value='';$('fileName').textContent='未选择文件';loadFiles()};
  x.onerror=()=>{setBusy(false);setMsg('上传失败：连接中断')};
  x.ontimeout=()=>{setBusy(false);setMsg('上传失败：连接超时')};
  x.timeout=0;x.send(f);
}
