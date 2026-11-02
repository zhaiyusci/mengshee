"""Windows opt-in integration test. Only terminates processes it created.

Requires pypdf and a deployed multifile build. Uses a unique coordinator
namespace and offscreen Qt windows, never the installed application's service.
"""
import argparse
import base64
import concurrent.futures
import ctypes as c
from ctypes import wintypes as w
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import shutil
import time
import uuid
from pypdf import PdfReader, PdfWriter
from pypdf.generic import ContentStream

p = argparse.ArgumentParser()
p.add_argument('--app', required=True)
p.add_argument('--profile', required=True)
p.add_argument('--texmf', required=True)
p.add_argument('--output', required=True)
p.add_argument('--qt-plugins', required=True)
p.add_argument('--reset-count', type=int, default=4)
a = p.parse_args()
app = Path(a.app).resolve()
out = Path(a.output).resolve(); out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
nonce = str(uuid.uuid4())
env.update(MENGSHEE_MULTIFILE_INSTANCE=nonce, QT_QPA_PLATFORM='offscreen', QT_PLUGIN_PATH=a.qt_plugins,
           MENGSHEE_STEMTEX_PROFILE_ROOT=Path(a.profile).resolve().as_posix(),
           MENGSHEE_STEMTEX_TEXMF_ROOT=Path(a.texmf).resolve().as_posix())
identity = app.as_posix() + Path(os.environ['LOCALAPPDATA']).as_posix() + nonce
pipe = '\\\\.\\pipe\\mengshee-multifile-v1-' + hashlib.sha256(identity.encode()).hexdigest()[:24]
k = c.WinDLL('kernel32', use_last_error=True)
k.CreateFileW.argtypes = [w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE]; k.CreateFileW.restype = w.HANDLE
k.ReadFile.argtypes = [w.HANDLE,c.c_void_p,w.DWORD,c.POINTER(w.DWORD),c.c_void_p]
k.WriteFile.argtypes = k.ReadFile.argtypes
k.CloseHandle.argtypes = [w.HANDLE]
k.OpenProcess.argtypes = [w.DWORD,w.BOOL,w.DWORD]; k.OpenProcess.restype = w.HANDLE
k.QueryFullProcessImageNameW.argtypes = [w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
k.TerminateProcess.argtypes = [w.HANDLE,w.UINT]
k.GetExitCodeProcess.argtypes = [w.HANDLE,c.POINTER(w.DWORD)]
k.PeekNamedPipe.argtypes = [w.HANDLE,c.c_void_p,w.DWORD,c.c_void_p,c.POINTER(w.DWORD),c.c_void_p]
nt = c.WinDLL('ntdll')
nt.NtSuspendProcess.argtypes = [w.HANDLE]
nt.NtSuspendProcess.restype = w.LONG
owned = set()

def request(obj):
    h = k.CreateFileW(pipe, 0xC0000000, 0, None, 3, 0, None)
    if h == w.HANDLE(-1).value: raise OSError(c.get_last_error(), 'connect')
    try:
        data = json.dumps(obj, separators=(',', ':')).encode() + b'\n'
        count = w.DWORD()
        assert k.WriteFile(h, data, len(data), c.byref(count), None), c.get_last_error()
        assert count.value == len(data)
        reply = bytearray()
        deadline = time.monotonic() + 195
        while True:
            available = w.DWORD()
            if not k.PeekNamedPipe(h, None, 0, None, c.byref(available), None):
                raise OSError(c.get_last_error(), 'response pipe closed')
            if not available.value:
                if time.monotonic() > deadline: raise TimeoutError('Coordinator response timed out')
                time.sleep(.01)
                continue
            chunk = c.create_string_buffer(65536)
            if not k.ReadFile(h, chunk, len(chunk), c.byref(count), None):
                assert c.get_last_error() == 234, c.get_last_error()
            assert count.value
            reply += chunk.raw[:count.value]
            assert len(reply) < 96*1024*1024
            while b'\n' in reply:
                line,_,tail=reply.partition(b'\n');reply=bytearray(tail)
                message=json.loads(line)
                if message.get('pending') is True: continue
                return message
    finally: k.CloseHandle(h)

def wait(predicate, seconds=120):
    until = time.monotonic()+seconds
    while time.monotonic() < until:
        try:
            value = predicate()
            if value: return value
        except (OSError, AssertionError): pass
        time.sleep(.25)
    raise AssertionError('Timed out waiting for condition')

def status(): return request({'op':'status'})

def alive(pid):
    h = k.OpenProcess(0x1000, False, pid)
    if not h: return False
    try:
        buf = c.create_unicode_buffer(32768); size = w.DWORD(len(buf))
        return bool(k.QueryFullProcessImageNameW(h, 0, buf, c.byref(size))) and Path(buf.value).resolve() == app
    finally: k.CloseHandle(h)

def terminate(pid):
    assert pid in owned and alive(pid), pid
    h = k.OpenProcess(1, False, pid)
    try: assert h and k.TerminateProcess(h, 91)
    finally:
        if h: k.CloseHandle(h)

def running(pid):
    h = k.OpenProcess(0x1000, False, pid)
    if not h: return False
    try:
        code = w.DWORD()
        return bool(k.GetExitCodeProcess(h, c.byref(code))) and code.value == 259
    finally: k.CloseHandle(h)

def config(profile):
    fields = dict(runtime=(app.parent.parent/'StemTeX/runtime').as_posix(), profile=profile.as_posix(), texmf=Path(a.texmf).resolve().as_posix())
    identity = json.dumps(fields, sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode()
    identity += (profile/'preamble.tex').read_bytes() + (profile/'warmup.tex').read_bytes()
    identity += (Path(__file__).resolve().parents[1]/'part/latexworker.tex').read_bytes()
    return dict(fields, key=hashlib.sha256(identity).hexdigest())

def render(settings, color, source='$$x^2$$'):
    response = request(dict(settings, op='render', color=color, source=source, width=240, font=10))
    assert response.get('ok'), response
    data = base64.b64decode(response['pdf'])
    page = PdfReader(io.BytesIO(data)).pages[0]
    assert len(PdfReader(io.BytesIO(data)).pages) == 1
    painted = [tuple(float(v) for v in operands) for operands, op in ContentStream(page.get_contents(),page.pdf).operations if op == b'rg']
    wanted = (1.,0.,0.) if color == '#ff0000' else (0.,0.,1.)
    assert wanted in painted, (color, painted)
    (out/('red.pdf' if color=='#ff0000' else 'blue.pdf')).write_bytes(data)
    return float(page.mediabox.height)

def processes():
    command = "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,CommandLine | ConvertTo-Json -Compress"
    return json.loads(subprocess.check_output(['powershell','-NoProfile','-Command',command], creationflags=0x08000000))

def engines(pid):
    rows = processes()
    descendants = {pid}
    for _ in range(8): descendants.update(r['ProcessId'] for r in rows if r['ParentProcessId'] in descendants)
    return [r['ProcessId'] for r in rows if r['ProcessId'] in descendants and r['Name'].lower()=='xetexdaemon.exe']

def renderer_host(broker):
    matches = [r['ProcessId'] for r in processes() if r['ParentProcessId']==broker and '--stemtex-service' in (r['CommandLine'] or '')]
    assert len(matches)==1, matches
    pid=matches[0]
    assert alive(pid)
    owned.add(pid)
    return pid

def suspend_owned(pid):
    assert pid in owned and alive(pid)
    h=k.OpenProcess(0x0800,False,pid)
    try: assert h and nt.NtSuspendProcess(h)==0
    finally:
        if h: k.CloseHandle(h)

try:
    files=[]
    for i in range(2):
        f=out/f'document-{i}.pdf'; writer=PdfWriter();writer.add_blank_page(300,400);writer.write(f);files.append(str(f))
    log=open(out/'processes.log','wb')
    launch=subprocess.Popen([str(app), *files],env=env,stdout=log,stderr=log,creationflags=0x08000000)
    assert launch.wait(timeout=30)==0
    first=wait(lambda: (s if len(s.get('documents',[]))==2 else None) if (s:=status()) else None)
    broker=int(first['pid']); docs={int(pid) for pid in first['documents']};owned.update(docs|{broker})
    assert len(docs)==2 and broker not in docs and all(alive(pid) for pid in docs)
    settings=config(Path(a.profile).resolve())
    warmed=wait(lambda: settings['key'] in status().get('profiles',[]),180)
    assert len(engines(broker))==1, engines(broker)
    print('PASS: two document processes share one coordinator and one hot XeTeX',flush=True)
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        red=pool.submit(render,settings,'#ff0000');blue=pool.submit(render,settings,'#0000ff')
        assert abs(red.result()-blue.result())<.01
    print('PASS: concurrent clients retain their colors and geometry',flush=True)
    alternate = out/'alternate-profile'
    shutil.copytree(a.profile, alternate, dirs_exist_ok=True)
    with (alternate/'preamble.tex').open('ab') as f: f.write(b'\n% Separate test profile\n')
    other = config(alternate)
    # A profile change replaces the one global host, never adds a second one.
    render(other,'#0000ff')
    assert len(engines(broker)) == 1
    render(settings,'#ff0000')
    assert len(engines(broker)) == 1
    for iteration in range(a.reset_count):
        before=set(engines(broker))
        started=time.monotonic()
        response=request(dict(settings,op='reset'))
        assert response.get('ok'), response
        elapsed=time.monotonic()-started
        assert elapsed < 60, 'Profile reset stalled'
        render(settings,'#0000ff')
        after=set(engines(broker))
        assert len(after) == 1 and not (before & after)
        wait(lambda: all(not running(pid) for pid in before-after),10)
        print(f'PASS: reset {iteration+1} completed in {elapsed:.2f}s; old worker exited, one shared worker remains',flush=True)
    print('PASS: global profile changes and repeated resets retain exactly one shared engine',flush=True)
    startup_stuck=renderer_host(broker)
    suspend_owned(startup_stuck)
    started=time.monotonic()
    response=request(dict(settings,op='warmup'))
    elapsed=time.monotonic()-started
    assert not response.get('ok') and '60 seconds' in response.get('error',''), response
    assert 59 <= elapsed < 65, elapsed
    wait(lambda: not running(startup_stuck),5)
    render(settings,'#ff0000')
    print(f'PASS: frozen warmup ends after {elapsed:.2f}s; progress frames keep the waiting client connected',flush=True)
    timedout=renderer_host(broker)
    suspend_owned(timedout)
    started=time.monotonic()
    response=request(dict(settings,op='render',source='$$x$$',color='#ff0000',width=240,font=10))
    elapsed=time.monotonic()-started
    assert not response.get('ok') and '10 seconds' in response.get('error',''), response
    assert 9 <= elapsed < 14, elapsed
    wait(lambda: not running(timedout),5)
    render(settings,'#0000ff')
    print(f'PASS: frozen ordinary typesetting ends after {elapsed:.2f}s and the next request recovers',flush=True)
    stuck=renderer_host(broker)
    old_workers=engines(broker)
    suspend_owned(stuck)
    started=time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(1) as pool:
        blocked=pool.submit(request,dict(settings,op='render',source='$$x$$',color='#ff0000',width=240,font=10))
        wait(lambda: status().get('pending',0)>0,10)
        response=request(dict(settings,op='reset'))
        interrupted=blocked.result(timeout=10)
        assert not interrupted.get('ok') and 'restart' in interrupted.get('error',''), interrupted
    assert response.get('ok'), response
    assert time.monotonic()-started < 60
    wait(lambda: not running(stuck) and all(not running(pid) for pid in old_workers),10)
    assert all(alive(pid) for pid in docs)
    render(settings,'#ff0000')
    print('PASS: restart interrupts an active request in a frozen renderer; both documents survive',flush=True)
    crashed=renderer_host(broker)
    terminate(crashed)
    response=request(dict(settings,op='render',source='$$x$$',color='#ff0000',width=240,font=10))
    assert not response.get('ok'), response
    render(settings,'#0000ff')
    assert all(alive(pid) for pid in docs)
    print('PASS: renderer crash reports failure and the next request restores typesetting',flush=True)
    bad=request(dict(settings,op='render',source=r'\undefinedMengsheeTestCommand',color='#ff0000',width=240,font=10))
    assert not bad.get('ok')
    assert request({'op':'status','key':settings['key']}).get('ready'), 'A TeX input error must not mark the shared service as starting'
    render(settings,'#0000ff')
    assert all(alive(pid) for pid in docs)
    print('PASS: TeX error recovery does not terminate documents',flush=True)
    victim=next(iter(docs));terminate(victim);docs.remove(victim)
    wait(lambda: len(status().get('documents',[]))==1)
    assert all(alive(pid) for pid in docs) and alive(broker)
    render(settings,'#ff0000')
    print('PASS: killing one document leaves the other and shared renderer usable',flush=True)
    old_workers=engines(broker)
    terminate(broker)
    wait(lambda: all(not running(pid) for pid in old_workers),10)
    replacement=wait(lambda: (s if int(s.get('pid',broker))!=broker and len(s.get('documents',[]))==1 else None) if (s:=status()) else None)
    broker=int(replacement['pid']);owned.add(broker)
    assert all(alive(pid) for pid in docs)
    render(settings,'#0000ff')
    assert len(engines(broker))==1
    print('PASS: coordinator crash leaves document alive; reconnect and rendering recover',flush=True)
    final_workers=engines(broker)
    for pid in list(docs): terminate(pid)
    wait(lambda: not alive(broker),30)
    wait(lambda: all(not running(pid) for pid in final_workers),10)
    print('PASS: coordinator exits after the last document closes',flush=True)
finally:
    for pid in list(owned):
        if alive(pid): terminate(pid)
