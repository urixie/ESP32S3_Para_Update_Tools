#include "app_web_file_server.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_storage_lock.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#define APP_WEB_MAX_PATH 320
#define APP_WEB_IO_BUF_SIZE 2048
#define APP_WEB_UPLOAD_BUF_SIZE 4096
#define WEB_AUTH_USER "admin"
#define WEB_AUTH_PASS "admin"
#define WEB_AUTH_SESSION_BYTES 32
#define WEB_AUTH_SESSION_HEX_LEN (WEB_AUTH_SESSION_BYTES * 2)
#define WEB_AUTH_SESSION_TIMEOUT_US (30LL * 60LL * 1000000LL)
#define WEB_FW_METADATA_SIZE 2000
#define WEB_FW_HASH_PART_SIZE 16
#define WEB_FW_ESP_IMAGE_MAGIC 0xE9

static const char *TAG = "web_file";
static httpd_handle_t s_server;
static char s_mount_point[64];
static char s_session_id[WEB_AUTH_SESSION_HEX_LEN + 1];
static bool s_session_valid;
static int64_t s_session_expire_us;

typedef enum {
    WEB_FW_TYPE_OTHER = 0,
    WEB_FW_TYPE_ANLOGIC,
    WEB_FW_TYPE_LATTICE,
    WEB_FW_TYPE_SMARTFUSION2,
    WEB_FW_TYPE_EFM8,
    WEB_FW_TYPE_ESP32S3,
} web_fw_type_t;

typedef struct {
    web_fw_type_t type;
    const char *type_name;
    const char *label;
} web_fw_info_t;

static void log_heap_state(const char *where)
{
    ESP_LOGI(TAG, "heap %s: free=%lu, min=%lu, internal=%u, psram=%u",
             where,
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void set_connection_close(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Connection", "close");
}

static const char s_login_html[] =
"<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>UniEdge 登录</title><style>:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#10151c;color:#eef3f8;font-family:Arial,'Microsoft YaHei',sans-serif}.card{width:min(390px,calc(100% - 28px));padding:28px;border:1px solid #303948;border-radius:16px;background:#181f29}.mark{width:42px;height:42px;border-radius:12px;background:linear-gradient(135deg,#2f81f7,#e7c477);margin-bottom:16px}h1{margin:0 0 6px;font-size:26px}.sub{margin:0 0 22px;color:#9ba8b7}.field{display:grid;gap:7px;margin:13px 0}label{font-size:14px;color:#c7d1dd}input{width:100%;padding:12px;border:1px solid #303948;border-radius:10px;background:#10151c;color:#eef3f8;outline:0}input:focus{border-color:#58a6ff}button{width:100%;"
"margin-top:8px;padding:12px;border:0;border-radius:10px;background:#2f81f7;color:white;font-weight:700}.msg{min-height:22px;margin-top:12px;color:#e7c477}</style></head><body><main class=\"card\"><div class=\"mark\"></div><h1>UniEdge 管理后台</h1><p class=\"sub\">文件系统管理</p><div class=\"field\"><label for=\"u\">用户名</label><input id=\"u\" value=\"admin\" autocomplete=\"username\"></div><div class=\"field\"><label for=\"p\">密码</label><input id=\"p\" type=\"password\" autocomplete=\"current-password\"></div><button onclick=\"login()\">登录</button><div id=\"m\" class=\"msg\"></div></main><script>const $=id=>document.getElementById(id);$('p').addEventListener('keydown',e=>{if(e.key==='Enter')login();});async function err(r){try{const j=await r.clone().json();return j.error||r.statusText}catch(e){return await r.text()||r.statusText}}async function login(){$('m').textContent='正在登录...';const body='username='+encodeURIComponent($('u"
"').value)+'&password='+encodeURIComponent($('p').value);try{const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!r.ok){$('m').textContent=await err(r);return;}location.href='/app';}catch(e){$('m').textContent='登录失败：'+e.message;}}</script></body></html>";

static const char s_app_html[] =
"<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>UniEdge 文件管理</title><style>:root{color-scheme:dark;--bg:#10151c;--panel:#181f29;--line:#303948;--text:#eef3f8;--mut:#9ba8b7;--blue:#2f81f7;--gold:#e7c477;--red:#ff7b72;--green:#78d99a}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:Arial,'Microsoft YaHei',sans-serif}button,input,.file-label{font:inherit}button,.file-label{border:1px solid var(--line);border-radius:8px;padding:8px 12px;color:#fff;background:#242c38;cursor:pointer}.primary{border:0;background:var(--blue)}.danger{border-color:#7d3734;color:#ffd7d3;background:#3a2023}.hidden{display:none!important}.app{max-width:1120px;margin:0 auto;padding:18px 12px}.top{display:flex;justify-content:space-between;gap:10px;align-items:center;margin-bottom:12px}h1{font-size:23px;margin:0}p{margin:4px 0;color:var(--mut)}.section{border:1px solid var(--line);border-rad"
"ius:10px;background:var(--panel);padding:14px;margin:12px 0}.upload{display:flex;gap:9px;align-items:center;flex-wrap:wrap}.muted{color:var(--mut)}input[type=file]{display:none}input[type=text]{padding:10px;border:1px solid var(--line);border-radius:8px;background:#10151c;color:var(--text)}.layout{display:grid;grid-template-columns:280px 1fr;gap:12px}.side{border:1px solid var(--line);border-radius:12px;background:#141a22;padding:8px}.cat{width:100%;min-height:58px;display:flex;align-items:center;justify-content:space-between;gap:12px;margin:5px 0;padding:10px 12px;border:1px solid transparent;border-radius:11px;background:transparent;text-align:left}.cat.active{background:#22324a;border-color:#2d4468}.cat-name{min-width:0;flex:1;display:flex;flex-direction:column;gap:3px;line-height:1.12}.cat-main{font-size:15px;font-weight:700;color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.cat-sub{font-size:13px;color:var(--mut);white-space:nowrap;overflow:hidden;text-ov"
"erflow:ellipsis}.count{min-width:24px;text-align:right;color:var(--gold);font-weight:700;font-variant-numeric:tabular-nums}.head{display:flex;justify-content:space-between;gap:8px;margin-bottom:10px}.head h2{font-size:19px;margin:0}.msg{min-height:22px;color:#e7c477}.empty{border:1px dashed #3a4555;border-radius:10px;padding:28px 12px;text-align:center;color:var(--mut)}.table{overflow:auto;border:1px solid #2d3643;border-radius:10px}table{width:100%;border-collapse:collapse}th,td{padding:10px;border-bottom:1px solid #2d3643;text-align:left;white-space:nowrap}th{color:var(--gold)}td.name{font-weight:700}.actions{display:flex;gap:7px}.actions a{border:1px solid var(--line);border-radius:8px;padding:7px 10px;color:var(--green);text-decoration:none;background:#202733}@media(max-width:760px){.layout{grid-temp"
"late-columns:1fr}.top{align-items:flex-start}.actions{align-items:stretch}.actions a,.actions button{width:100%;text-align:center}}</style></head><body><main class=\"app\"><header class=\"top\"><div><h1>UniEdge 文件管理</h1><p>浏览、上传、下载和删除 /disk 文件</p></div><button onclick=\"logout()\">退出登录</button></header><section class=\"section\"><div class=\"upload\"><label class=\"file-label\" for=\"file\">选择文件</label><input id=\"file\" type=\"file\"><span id=\"fileName\" class=\"muted\">未选择文件</span><button id=\"uploadBtn\" class=\"primary\" onclick=\"upload()\">上传文件</button><button id=\"refreshBtn\" onclick=\"loadFiles()\">刷新列表</button></div><div id=\"msg\" class=\"msg\"></div></section><section class=\"layout\"><nav id=\"catNav\" class=\"side\"></nav><section class=\"section\"><div class=\"head\"><div><h2 id=\"categoryTitle\">ESPRESSIF ESP32S3</h2><p id=\"categoryHint\">正在加载</p></div></div><div id=\"emptyState\" class=\"empty hidden\">暂无该类文件，请上传文件</div><div id=\"tableWrap\" class=\"table\"><table><thead><tr><th>文件名</th><th>大小</th><th>操作</th></tr></thead><tbody id=\"list\"></tbody></table></div></section></section></main><script>const $=id=>document.getElementById(id);let allFiles=[],currentFwType='esp32s3',uploading=false;const fwGroups=[['esp32s3','ESPRESSIF','ESP32S3'],['anlogic','ANLOGIC','EF2'],['lattice','LATTICE','MXO2'],['smartfusion2','MICROCHIP','SmartFusion2'],['efm8','Silicon Labs','EFM8'],['other','其他文件','']];$('file').addEventListener('change',()=>{$('fileName').textContent=$('file').files[0]?'已选择：'+$('file').files[0].name:'未选择文件';});$('catNav').addEventListener('click',e=>{const b=e.target.closest('button[data-key]');if(b)selectCategory(b.dataset.key);});$('list').addEventListener('click',e="
">{const d=e.target.closest('button[data-del]');if(d)del(d.dataset.del);});function esc(s){return String(s||'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));}function setMsg(s){$('msg').textContent=s||''}function fmt(n){n=Number(n)||0;if(n<1024)return n+' B';if(n<1048576)return(n/1024).toFixed(1)+' KB';return(n/1048576).toFixed(1)+' MB'}async function readErr(r){try{const j=await r.clone().json();return j.error||r.statusText}catch(e){const t=await r.text();return t||r.statusText}}function xhrErrorText(x){try{const j=JSON.parse(x.responseText||'{}');return j.error||x.statusText}catch(e){return x.responseText||x.statusText||'连接失败'}}function setBusy(b){uploading=b;$('uploadBtn').disabled=b;$('refreshBtn').disabled=b}async function checkAuth(){try{const r=await fetch('/api/auth/status');if(!r.ok){location.href='/'"
";return;}await loadFiles();}catch(e){location.href='/'}}function filesFor(k){return allFiles.filter(f=>(f.firmware_type||'other')===k)}function title(k){const g=fwGroups.find(x=>x[0]===k);if(!g)return '其他文件';return g[2]?g[1]+' '+g[2]:g[1]}function renderCategories(){$('catNav').innerHTML=fwGroups.map(([k,main,sub])=>'<button class=\"cat '+(k===currentFwType?'active':'')+'\" data-key=\"'+k+'\"><span class=\"cat-name\"><span class=\"cat-main\">'+main+'</span>'+(sub?'<span class=\"cat-sub\">'+sub+'</span>':'')+'</span><span class=\"count\">'+filesFor(k).length+'</span></button>').join('')}function selectCategory(k){currentFwType=k;renderCategories();renderCurrent()}function row(f){const p=encodeURIComponent(f.path);let a='';if(!f.is_dir){a='<div class=\"actions\"><a href=\"/download?path='+p+'\">下载</a>';a+='<button class=\"danger\" data-del=\"'+p+'\">删除</button></div>'}return'<tr><td class=\"name\">'+esc(f.name)+'</td><"
"td>'+(f.is_dir?'目录':fmt(f.size))+'</td><td>'+a+'</td></tr>'}function renderCurrent(){const fs=filesFor(currentFwType);$('categoryTitle').textContent=title(currentFwType);$('categoryHint').textContent='当前分类 '+fs.length+' 个项目';$('emptyState').classList.toggle('hidden',fs.length>0);$('tableWrap').classList.toggle('hidden',fs.length===0);$('list').innerHTML=fs.map(row).join('')}async function loadFiles(){if(uploading){setMsg('文件正在上传，请稍后刷新');return;}setMsg('正在加载文件列表...');try{const r=await fetch('/files');const j=await r.json();if(r.status===401){location.href='/';return;}if(!r.ok)throw new Error(j.error||r.statusText);allFiles=(j.files||[]).filter(f=>f.name!=='System Volume Information');renderCategories();renderCurrent();setMsg('文件列表已更新，共 '+allFiles.length+' 个项目')}catch(e){setMsg('加载失败：'+e.message)}}function upload(){if(uploading){setMsg('文件正在上传，请稍候');return;}const f=$('file').files[0];if(!f){setMsg('请先选择一个文件');return;}setB"
"usy(true);setMsg('准备上传...');const x=new XMLHttpRequest();x.open('POST','/upload?filename='+encodeURIComponent(f.name),true);x.upload.onprogress=e=>{setMsg(e.lengthComputable?'正在上传 '+Math.floor(e.loaded*100/e.total)+'%（'+fmt(e.loaded)+' / '+fmt(e.total)+'）':'正在上传文件...')};x.onload=()=>{setBusy(false);if(x.status===401){location.href='/';return;}if(x.status<200||x.status>=300){setMsg('上传失败：'+xhrErrorText(x));return;}setMsg('上传完成');$('file').value='';$('fileName').textContent='未选择文件';loadFiles()};x.onerror=()=>{setBusy(false);setMsg('上传失败：连接中断')};x.ontimeout=()=>{setBusy(false);setMsg('上传失败：连接超时')};x.timeout=0;x.send(f)}async function del(p){if(!confirm('确认删除这个文件吗？'))return;setMsg('正在删除文件...');const r=await fetch('/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+p});if(r.status===401){location.href='/';return;}if(!r.ok){setMsg('删除失败：'+await readErr(r));return;}setMsg('删除完成');loadFiles()}async function logout(){try{await fetch('/api/logout',{method:'POST'})}catch(e){}location.href='/'}renderCategories();renderCurrent();checkAuth();</script></body></html>";

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static esp_err_t url_decode(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;
    for (size_t i = 0; src[i] != '\0'; i++) {
        if (out + 1 >= dst_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (src[i] == '%' && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            dst[out++] = (char)((hi << 4) | lo);
            i += 2;
        } else if (src[i] == '+') {
            dst[out++] = ' ';
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
    return ESP_OK;
}

static bool has_bad_path_chars(const char *path)
{
    return strstr(path, "..") != NULL || strchr(path, '\\') != NULL || strchr(path, ':') != NULL;
}

static esp_err_t build_storage_path(const char *path, bool allow_root, char *out, size_t out_size)
{
    char decoded[APP_WEB_MAX_PATH];
    ESP_RETURN_ON_ERROR(url_decode(decoded, sizeof(decoded), path != NULL ? path : ""), TAG, "decode failed");

    while (decoded[0] == '/') {
        memmove(decoded, decoded + 1, strlen(decoded));
    }

    if (decoded[0] == '\0') {
        if (!allow_root) {
            return ESP_ERR_INVALID_ARG;
        }
        int len = snprintf(out, out_size, "%s", s_mount_point);
        return (len > 0 && (size_t)len < out_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }

    if (has_bad_path_chars(decoded)) {
        return ESP_ERR_INVALID_ARG;
    }

    int len = snprintf(out, out_size, "%s/%s", s_mount_point, decoded);
    if (len <= 0 || (size_t)len >= out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t get_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
{
    char query[APP_WEB_MAX_PATH];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (httpd_query_key_value(query, key, out, out_size) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static bool valid_upload_name(const char *name)
{
    if (name == NULL || name[0] == '\0' || has_bad_path_chars(name)) {
        return false;
    }
    for (const char *p = name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || *p == '/' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            return false;
        }
    }
    return true;
}

static void json_escape_send(httpd_req_t *req, const char *text)
{
    for (const char *p = text; *p != '\0'; p++) {
        char esc[7];
        switch (*p) {
        case '\\':
            httpd_resp_sendstr_chunk(req, "\\\\");
            break;
        case '"':
            httpd_resp_sendstr_chunk(req, "\\\"");
            break;
        case '\n':
            httpd_resp_sendstr_chunk(req, "\\n");
            break;
        case '\r':
            httpd_resp_sendstr_chunk(req, "\\r");
            break;
        case '\t':
            httpd_resp_sendstr_chunk(req, "\\t");
            break;
        default:
            if ((unsigned char)*p < 0x20) {
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
                httpd_resp_sendstr_chunk(req, esc);
            } else {
                httpd_resp_send_chunk(req, p, 1);
            }
            break;
        }
    }
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    httpd_resp_sendstr_chunk(req, "{\"ok\":false,\"error\":\"");
    json_escape_send(req, message);
    httpd_resp_sendstr_chunk(req, "\"}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t send_html_chunked(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    set_connection_close(req);

    const size_t chunk_size = 1024;
    size_t len = strlen(html);
    size_t off = 0;

    while (off < len) {
        size_t n = len - off;
        if (n > chunk_size) {
            n = chunk_size;
        }
        esp_err_t ret = httpd_resp_send_chunk(req, html + off, n);
        if (ret != ESP_OK) {
            return ret;
        }
        off += n;
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t redirect_to_login(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    set_connection_close(req);
    return httpd_resp_send(req, NULL, 0);
}

static void web_auth_refresh_expire(void)
{
    s_session_expire_us = esp_timer_get_time() + WEB_AUTH_SESSION_TIMEOUT_US;
}

static void web_auth_clear_session(void)
{
    memset(s_session_id, 0, sizeof(s_session_id));
    s_session_valid = false;
    s_session_expire_us = 0;
}

static void web_auth_create_session(void)
{
    uint8_t random_bytes[WEB_AUTH_SESSION_BYTES];
    static const char hex[] = "0123456789abcdef";

    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        s_session_id[i * 2] = hex[random_bytes[i] >> 4];
        s_session_id[i * 2 + 1] = hex[random_bytes[i] & 0x0f];
    }
    s_session_id[WEB_AUTH_SESSION_HEX_LEN] = '\0';
    s_session_valid = true;
    web_auth_refresh_expire();
}

static bool cookie_has_sid(const char *cookie_header, const char *sid)
{
    const char *p = cookie_header;
    size_t sid_len = strlen(sid);

    while (p != NULL && *p != '\0') {
        while (*p == ' ' || *p == ';') {
            p++;
        }

        const char *end = strchr(p, ';');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        while (len > 0 && isspace((unsigned char)p[len - 1])) {
            len--;
        }

        const char key[] = "sid=";
        if (len == sizeof(key) - 1 + sid_len &&
            strncmp(p, key, sizeof(key) - 1) == 0 &&
            memcmp(p + sizeof(key) - 1, sid, sid_len) == 0) {
            return true;
        }

        p = end != NULL ? end + 1 : NULL;
    }

    return false;
}

static bool web_auth_is_logged_in(httpd_req_t *req)
{
    if (!s_session_valid || s_session_id[0] == '\0') {
        return false;
    }
    if (esp_timer_get_time() > s_session_expire_us) {
        web_auth_clear_session();
        return false;
    }

    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (cookie_len == 0) {
        return false;
    }

    char *cookie = malloc(cookie_len + 1);
    if (cookie == NULL) {
        return false;
    }

    bool ok = false;
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, cookie_len + 1) == ESP_OK) {
        ok = cookie_has_sid(cookie, s_session_id);
    }
    free(cookie);

    if (ok) {
        web_auth_refresh_expire();
    }
    return ok;
}

static esp_err_t read_form_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (req->content_len <= 0 || req->content_len >= body_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    int remaining = req->content_len;
    int offset = 0;
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, body + offset, remaining);
        if (recv_len <= 0) {
            return ESP_FAIL;
        }
        remaining -= recv_len;
        offset += recv_len;
    }
    body[offset] = '\0';
    return ESP_OK;
}

static esp_err_t get_form_value_decoded(const char *body, const char *key, char *out, size_t out_size)
{
    char encoded[APP_WEB_MAX_PATH];
    if (httpd_query_key_value(body, key, encoded, sizeof(encoded)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    return url_decode(out, out_size, encoded);
}

static esp_err_t index_handler(httpd_req_t *req)
{
    log_heap_state("before index");
    if (web_auth_is_logged_in(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/app");
        set_connection_close(req);
        return httpd_resp_send(req, NULL, 0);
    }
    return send_html_chunked(req, s_login_html);
}

static esp_err_t app_handler(httpd_req_t *req)
{
    log_heap_state("before app");
    if (!web_auth_is_logged_in(req)) {
        return redirect_to_login(req);
    }
    return send_html_chunked(req, s_app_html);
}

static esp_err_t login_handler(httpd_req_t *req)
{
    char body[APP_WEB_MAX_PATH];
    char username[64];
    char password[96];

    if (read_form_body(req, body, sizeof(body)) != ESP_OK ||
        get_form_value_decoded(body, "username", username, sizeof(username)) != ESP_OK ||
        get_form_value_decoded(body, "password", password, sizeof(password)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "登录请求格式错误");
    }

    if (strcmp(username, WEB_AUTH_USER) != 0 || strcmp(password, WEB_AUTH_PASS) != 0) {
        return send_json_error(req, "401 Unauthorized", "用户名或密码错误");
    }

    if (s_session_valid && esp_timer_get_time() > s_session_expire_us) {
        web_auth_clear_session();
    }

    if (s_session_valid && s_session_id[0] != '\0') {
        if (web_auth_is_logged_in(req)) {
            httpd_resp_set_type(req, "application/json; charset=utf-8");
            set_connection_close(req);
            return httpd_resp_sendstr(req, "{\"ok\":true}");
        }
        return send_json_error(req, "409 Conflict", "已有用户登录，请先退出当前会话或等待会话过期");
    }

    web_auth_create_session();

    char cookie[128];
    snprintf(cookie, sizeof(cookie), "sid=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=1800", s_session_id);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t logout_handler(httpd_req_t *req)
{
    if (!s_session_valid || web_auth_is_logged_in(req)) {
        web_auth_clear_session();
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t auth_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    if (web_auth_is_logged_in(req)) {
        set_connection_close(req);
        return httpd_resp_sendstr(req, "{\"ok\":true,\"login\":true,\"user\":\"admin\"}");
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":false,\"login\":false}");
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    set_connection_close(req);
    return httpd_resp_send(req, NULL, 0);
}

static web_fw_info_t web_fw_make_info(web_fw_type_t type)
{
    switch (type) {
    case WEB_FW_TYPE_ANLOGIC:
        return (web_fw_info_t){.type = type, .type_name = "anlogic", .label = "UE-0102"};
    case WEB_FW_TYPE_LATTICE:
        return (web_fw_info_t){.type = type, .type_name = "lattice", .label = "LATTICE MXO2"};
    case WEB_FW_TYPE_SMARTFUSION2:
        return (web_fw_info_t){.type = type, .type_name = "smartfusion2", .label = "MICROCHIP SmartFusion2"};
    case WEB_FW_TYPE_EFM8:
        return (web_fw_info_t){.type = type, .type_name = "efm8", .label = "Silicon Labs EFM8"};
    case WEB_FW_TYPE_ESP32S3:
        return (web_fw_info_t){.type = type, .type_name = "esp32s3", .label = "ESPRESSIF ESP32S3"};
    default:
        return (web_fw_info_t){.type = WEB_FW_TYPE_OTHER, .type_name = "other", .label = "Other"};
    }
}

static web_fw_info_t detect_firmware_info(const char *full_path, const struct stat *st)
{
    if (S_ISDIR(st->st_mode) || st->st_size <= 0) {
        return web_fw_make_info(WEB_FW_TYPE_OTHER);
    }

    FILE *f = fopen(full_path, "rb");
    if (f == NULL) {
        return web_fw_make_info(WEB_FW_TYPE_OTHER);
    }

    uint32_t chip_model = 0;
    uint8_t magic = 0;
    if (st->st_size >= (WEB_FW_METADATA_SIZE + 2 * WEB_FW_HASH_PART_SIZE) &&
        fread(&chip_model, 1, sizeof(chip_model), f) == sizeof(chip_model)) {
        switch (chip_model) {
        case 1:
            fclose(f);
            return web_fw_make_info(WEB_FW_TYPE_ANLOGIC);
        case 2:
            fclose(f);
            return web_fw_make_info(WEB_FW_TYPE_LATTICE);
        case 3:
            fclose(f);
            return web_fw_make_info(WEB_FW_TYPE_SMARTFUSION2);
        case 4:
            fclose(f);
            return web_fw_make_info(WEB_FW_TYPE_EFM8);
        default:
            break;
        }
    }

    rewind(f);
    bool is_esp_image = fread(&magic, 1, sizeof(magic), f) == sizeof(magic) &&
                        magic == WEB_FW_ESP_IMAGE_MAGIC;
    fclose(f);

    if (is_esp_image) {
        return web_fw_make_info(WEB_FW_TYPE_ESP32S3);
    }

    return web_fw_make_info(WEB_FW_TYPE_OTHER);
}

static esp_err_t send_file_item(httpd_req_t *req, const char *relative_path, const char *name,
                                const char *full_path, const struct stat *st, bool *first)
{
    web_fw_info_t fw = detect_firmware_info(full_path, st);
    char meta[160];
    snprintf(meta, sizeof(meta), "%s{\"name\":\"", *first ? "" : ",");
    httpd_resp_sendstr_chunk(req, meta);
    json_escape_send(req, name);
    httpd_resp_sendstr_chunk(req, "\",\"path\":\"");
    json_escape_send(req, relative_path);
    snprintf(meta, sizeof(meta),
             "\",\"size\":%lld,\"is_dir\":%s,\"firmware_type\":\"%s\","
             "\"firmware_label\":\"%s\"}",
             (long long)st->st_size, S_ISDIR(st->st_mode) ? "true" : "false",
             fw.type_name, fw.label);
    httpd_resp_sendstr_chunk(req, meta);
    *first = false;
    return ESP_OK;
}

static const char *content_type_from_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext != NULL) {
        if (strcasecmp(ext, ".png") == 0) {
            return "image/png";
        }
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
            return "image/jpeg";
        }
        if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
            return "text/html; charset=utf-8";
        }
        if (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".log") == 0) {
            return "text/plain; charset=utf-8";
        }
    }
    return "application/octet-stream";
}

static esp_err_t list_dir_entries(httpd_req_t *req, const char *relative_dir, bool *first)
{
    char *full_dir = malloc(APP_WEB_MAX_PATH);
    char *relative_path = malloc(APP_WEB_MAX_PATH);
    char *full_path = malloc(APP_WEB_MAX_PATH);
    if (full_dir == NULL || relative_path == NULL || full_path == NULL) {
        free(full_dir);
        free(relative_path);
        free(full_path);
        return ESP_ERR_NO_MEM;
    }

    int len = relative_dir[0] == '\0'
                  ? snprintf(full_dir, APP_WEB_MAX_PATH, "%s", s_mount_point)
                  : snprintf(full_dir, APP_WEB_MAX_PATH, "%s/%s", s_mount_point, relative_dir);
    if (len <= 0 || (size_t)len >= APP_WEB_MAX_PATH) {
        free(full_dir);
        free(relative_path);
        free(full_path);
        return ESP_ERR_INVALID_SIZE;
    }

    DIR *dir = opendir(full_dir);
    if (dir == NULL) {
        free(full_dir);
        free(relative_path);
        free(full_path);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, "System Volume Information") == 0 ||
            has_bad_path_chars(entry->d_name)) {
            continue;
        }

        int rel_len = relative_dir[0] == '\0'
                          ? snprintf(relative_path, APP_WEB_MAX_PATH, "/%s", entry->d_name)
                          : snprintf(relative_path, APP_WEB_MAX_PATH, "/%s/%s", relative_dir, entry->d_name);
        if (rel_len <= 0 || (size_t)rel_len >= APP_WEB_MAX_PATH) {
            continue;
        }

        int full_len = snprintf(full_path, APP_WEB_MAX_PATH, "%s%s", s_mount_point, relative_path);
        if (full_len <= 0 || (size_t)full_len >= APP_WEB_MAX_PATH) {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        send_file_item(req, relative_path, entry->d_name, full_path, &st, first);
    }

    closedir(dir);
    free(full_dir);
    free(relative_path);
    free(full_path);
    return ESP_OK;
}

static esp_err_t files_handler(httpd_req_t *req)
{
    log_heap_state("before files");
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    ESP_LOGI(TAG, "list files");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);

    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }

    DIR *dir = opendir(s_mount_point);
    if (dir == NULL) {
        int err = errno;
        app_storage_unlock();
        ESP_LOGE(TAG, "opendir failed: errno=%d", err);
        return send_json_error(req, "500 Internal Server Error", "无法打开存储目录");
    }
    closedir(dir);

    httpd_resp_sendstr_chunk(req, "{\"mount_point\":\"");
    json_escape_send(req, s_mount_point);
    httpd_resp_sendstr_chunk(req, "\",\"files\":[");

    bool first = true;
    list_dir_entries(req, "", &first);

    app_storage_unlock();

    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t stream_file(httpd_req_t *req, const char *full_path, bool attachment)
{
    FILE *f = fopen(full_path, "rb");
    if (f == NULL) {
        return send_json_error(req, "404 Not Found", "文件不存在");
    }

    httpd_resp_set_type(req, content_type_from_path(full_path));
    set_connection_close(req);
    if (attachment) {
        char disposition[APP_WEB_MAX_PATH + 64];
        const char *name = strrchr(full_path, '/');
        snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name != NULL ? name + 1 : "download.bin");
        httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    }

    char *buf = malloc(APP_WEB_IO_BUF_SIZE);
    if (buf == NULL) {
        fclose(f);
        return send_json_error(req, "500 Internal Server Error", "内存不足");
    }

    esp_err_t ret = ESP_OK;
    size_t read_len;
    while ((read_len = fread(buf, 1, APP_WEB_IO_BUF_SIZE, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_len) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }

    free(buf);
    fclose(f);

    if (ret != ESP_OK) {
        return ret;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t download_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    char raw_path[APP_WEB_MAX_PATH];
    char full_path[APP_WEB_MAX_PATH];
    if (get_query_value(req, "path", raw_path, sizeof(raw_path)) != ESP_OK ||
        build_storage_path(raw_path, false, full_path, sizeof(full_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "路径无效");
    }

    ESP_LOGI(TAG, "download start: %s", full_path);
    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }
    esp_err_t ret = stream_file(req, full_path, true);
    app_storage_unlock();
    ESP_LOGI(TAG, "download done: %s", full_path);
    return ret;
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    if (req->content_len <= 0) {
        return send_json_error(req, "400 Bad Request", "上传内容为空");
    }

    char filename_encoded[APP_WEB_MAX_PATH];
    char filename[APP_WEB_MAX_PATH];
    if (get_query_value(req, "filename", filename_encoded, sizeof(filename_encoded)) != ESP_OK ||
        url_decode(filename, sizeof(filename), filename_encoded) != ESP_OK ||
        !valid_upload_name(filename)) {
        return send_json_error(req, "400 Bad Request", "文件名无效");
    }

    char full_path[APP_WEB_MAX_PATH];
    int len = snprintf(full_path, sizeof(full_path), "%s/%s", s_mount_point, filename);
    if (len <= 0 || (size_t)len >= sizeof(full_path)) {
        return send_json_error(req, "400 Bad Request", "文件名过长");
    }

    char tmp_path[APP_WEB_MAX_PATH];
    len = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", full_path);
    if (len <= 0 || (size_t)len >= sizeof(tmp_path)) {
        return send_json_error(req, "400 Bad Request", "文件名过长");
    }

    log_heap_state("upload start");
    ESP_LOGI(TAG, "upload start: %s, tmp=%s, size=%zu", full_path, tmp_path, req->content_len);
    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }

    FILE *f = fopen(tmp_path, "wb");
    if (f == NULL) {
        int err = errno;
        app_storage_unlock();
        ESP_LOGE(TAG, "open tmp upload file failed: %s, errno=%d", tmp_path, err);
        return send_json_error(req, "500 Internal Server Error", "创建文件失败");
    }

    char *buf = malloc(APP_WEB_UPLOAD_BUF_SIZE);
    if (buf == NULL) {
        fclose(f);
        remove(tmp_path);
        app_storage_unlock();
        return send_json_error(req, "500 Internal Server Error", "内存不足");
    }

    const size_t total = req->content_len;
    size_t remaining = total;
    size_t received = 0;
    size_t last_log = 0;
    int timeout_count = 0;
    const int max_timeout_count = 30;
    esp_err_t ret = ESP_OK;

    while (remaining > 0) {
        size_t want = remaining > APP_WEB_UPLOAD_BUF_SIZE ? APP_WEB_UPLOAD_BUF_SIZE : remaining;
        int recv_len = httpd_req_recv(req, buf, want);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT && timeout_count++ < max_timeout_count) {
                ESP_LOGW(TAG, "upload recv timeout %d/%d, received=%zu/%zu",
                         timeout_count, max_timeout_count, received, total);
                continue;
            }
            ESP_LOGE(TAG, "upload recv failed: recv_len=%d, received=%zu/%zu", recv_len, received, total);
            ret = recv_len == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
            break;
        }

        timeout_count = 0;
        if (fwrite(buf, 1, recv_len, f) != (size_t)recv_len) {
            int err = errno;
            ESP_LOGE(TAG, "upload write failed, received=%zu/%zu, errno=%d", received, total, err);
            ret = ESP_FAIL;
            break;
        }

        remaining -= recv_len;
        received += recv_len;
        if (received == total || received - last_log >= 64 * 1024) {
            ESP_LOGI(TAG, "upload progress: %zu/%zu", received, total);
            last_log = received;
        }
    }

    free(buf);

    if (ret == ESP_OK && received != total) {
        ESP_LOGE(TAG, "upload size mismatch: received=%zu/%zu", received, total);
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK && fflush(f) != 0) {
        int err = errno;
        ESP_LOGE(TAG, "upload fflush failed errno=%d", err);
        ret = ESP_FAIL;
    }

    if (fclose(f) != 0 && ret == ESP_OK) {
        int err = errno;
        ESP_LOGE(TAG, "upload fclose failed errno=%d", err);
        ret = ESP_FAIL;
    }
    f = NULL;

    if (ret == ESP_OK) {
        remove(full_path);
        if (rename(tmp_path, full_path) != 0) {
            int err = errno;
            ESP_LOGE(TAG, "upload rename failed: %s -> %s, errno=%d", tmp_path, full_path, err);
            ret = ESP_FAIL;
        }
    }

    if (ret != ESP_OK) {
        remove(tmp_path);
    }

    app_storage_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "upload failed: %s, received=%zu/%zu, err=%s",
                 full_path, received, total, esp_err_to_name(ret));
        return send_json_error(req, "500 Internal Server Error", "上传失败");
    }

    log_heap_state("upload done");
    ESP_LOGI(TAG, "upload done: %s, received=%zu", full_path, received);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t delete_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    char body[APP_WEB_MAX_PATH];
    if (read_form_body(req, body, sizeof(body)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少请求内容");
    }

    char encoded_path[APP_WEB_MAX_PATH] = {0};
    if (httpd_query_key_value(body, "path", encoded_path, sizeof(encoded_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少文件路径");
    }

    char full_path[APP_WEB_MAX_PATH];
    if (build_storage_path(encoded_path, false, full_path, sizeof(full_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "路径无效");
    }

    ESP_LOGI(TAG, "delete start: %s", full_path);
    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }

    struct stat st;
    if (stat(full_path, &st) != 0) {
        app_storage_unlock();
        return send_json_error(req, "404 Not Found", "文件不存在");
    }
    if (S_ISDIR(st.st_mode)) {
        app_storage_unlock();
        return send_json_error(req, "400 Bad Request", "暂不支持删除目录");
    }
    if (remove(full_path) != 0) {
        int err = errno;
        app_storage_unlock();
        ESP_LOGE(TAG, "delete failed errno=%d", err);
        return send_json_error(req, "500 Internal Server Error", "删除失败");
    }

    app_storage_unlock();
    ESP_LOGI(TAG, "delete done: %s", full_path);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}


esp_err_t app_web_file_server_start(const char *mount_point)
{
    ESP_RETURN_ON_FALSE(mount_point != NULL && mount_point[0] == '/', ESP_ERR_INVALID_ARG,
                        TAG, "invalid mount point");

    if (s_server != NULL) {
        return ESP_OK;
    }

    int len = snprintf(s_mount_point, sizeof(s_mount_point), "%s", mount_point);
    ESP_RETURN_ON_FALSE(len > 0 && (size_t)len < sizeof(s_mount_point), ESP_ERR_INVALID_SIZE,
                        TAG, "mount point too long");

    log_heap_state("before server start");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 12288;
    config.max_uri_handlers = 12;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 60;
    config.send_wait_timeout = 30;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    config.keep_alive_enable = false;
#endif

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd_start failed");
    log_heap_state("after server start");

    const httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    const httpd_uri_t app_uri = {.uri = "/app", .method = HTTP_GET, .handler = app_handler};
    const httpd_uri_t login_uri = {.uri = "/api/login", .method = HTTP_POST, .handler = login_handler};
    const httpd_uri_t logout_uri = {.uri = "/api/logout", .method = HTTP_POST, .handler = logout_handler};
    const httpd_uri_t auth_status_uri = {.uri = "/api/auth/status", .method = HTTP_GET, .handler = auth_status_handler};
    const httpd_uri_t favicon_uri = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler};
    const httpd_uri_t files_uri = {.uri = "/files", .method = HTTP_GET, .handler = files_handler};
    const httpd_uri_t download_uri = {.uri = "/download", .method = HTTP_GET, .handler = download_handler};
    const httpd_uri_t upload_uri = {.uri = "/upload", .method = HTTP_POST, .handler = upload_handler};
    const httpd_uri_t delete_uri = {.uri = "/delete", .method = HTTP_POST, .handler = delete_handler};

    esp_err_t ret = httpd_register_uri_handler(s_server, &index_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register / failed");
    ret = httpd_register_uri_handler(s_server, &app_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /app failed");
    ret = httpd_register_uri_handler(s_server, &login_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/login failed");
    ret = httpd_register_uri_handler(s_server, &logout_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/logout failed");
    ret = httpd_register_uri_handler(s_server, &auth_status_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/auth/status failed");
    ret = httpd_register_uri_handler(s_server, &favicon_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /favicon.ico failed");
    ret = httpd_register_uri_handler(s_server, &files_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /files failed");
    ret = httpd_register_uri_handler(s_server, &download_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /download failed");
    ret = httpd_register_uri_handler(s_server, &upload_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /upload failed");
    ret = httpd_register_uri_handler(s_server, &delete_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /delete failed");

    ESP_LOGI(TAG, "HTTP file server started at mount point %s", s_mount_point);
    return ESP_OK;

err_stop:
    httpd_stop(s_server);
    s_server = NULL;
    return ret;
}
