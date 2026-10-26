# SPDX-License-Identifier: GPL-2.0-or-later
import ctypes as c, pathlib, json, shutil, re
from pypdf import PdfReader
import argparse
parser=argparse.ArgumentParser(description="Opt-in native StemTeX block geometry regression; requires pypdf.")
for name in ['runtime', 'profile', 'texmf', 'fixtures', 'output']: parser.add_argument('--'+name, required=True)
args=parser.parse_args()
base=pathlib.Path(args.output).resolve();base.mkdir(parents=True,exist_ok=True)
root=pathlib.Path(args.runtime).resolve()
class Config(c.Structure):
 _fields_=[(x,c.c_char_p) for x in ['repo','runtime','texmf','profile','state','renders']]+[(x,c.c_int) for x in ['timeout','pdf_timeout']]+[(x,c.c_double) for x in ['min_width','max_width','default_width']]+[(x,c.c_int) for x in ['spare','restart','delete']]+[('template',c.c_char_p)]
class Result(c.Structure):
 _fields_=[(x,c.c_char_p) for x in ['id','path','format','summary']]+[('outcome',c.c_int),('flags',c.c_int),('message',c.c_char_p)]
dll=c.CDLL(str(root/'runtime/bin/sdk/stemtex-renderer.dll'))
dll.stemtex_renderer_create.argtypes=[c.POINTER(Config),c.POINTER(c.c_int),c.POINTER(c.c_char_p)];dll.stemtex_renderer_create.restype=c.c_void_p
dll.stemtex_renderer_render_block.argtypes=[c.c_void_p,c.c_char_p,c.c_double,c.c_double,c.c_int,c.POINTER(Result),c.POINTER(c.c_int),c.POINTER(c.c_char_p)]
dll.stemtex_renderer_destroy.argtypes=[c.c_void_p];dll.stemtex_renderer_free_output_result.argtypes=[c.POINTER(Result)];dll.stemtex_renderer_free_string.argtypes=[c.c_void_p]
config=Config(str(root/'runtime').encode(),str(root/'runtime').encode(),str(pathlib.Path(args.texmf).resolve()).encode(),str(pathlib.Path(args.profile).resolve()).encode(),str((base/'state').resolve()).encode(),str((base/'renders').resolve()).encode(),90000,90000,0,0,0,0,1,0,None)
err=c.c_int();msg=c.c_char_p();renderer=dll.stemtex_renderer_create(c.byref(config),c.byref(err),c.byref(msg));assert renderer,msg.value
fixtures=json.loads(pathlib.Path(args.fixtures).read_text(encoding='utf-8'))
selected=['lowercase-a','uppercase-a','descender-g','inline-alpha','inline-tall-fraction','multiple-paragraphs','pure-display-plus','pure-display-descender','display-current-colorlet','display-explicit-color-override','text-current-color','explicit-color-override']
chosen=[f for f in fixtures if any(f['name']==n+'-'+co for n in selected for co in ['black','red'])]
paragraph='这个投影算符的“像”（image），即所有可能的投影结果组成的集合：\n'+r'$\operatorname{im}P(q_0) =\{P(q_0)v:\ v\in\mathcal H_e\}.$'
prefix=fixtures[0]['prepared'][:-len(fixtures[0]['source']+'%\n')]
chosen.append(dict(name='chinese',prepared=prefix+paragraph+'%\n',width=300,fontSize=10))
rows=[]
try:
 for f in chosen:
  r=Result();m=c.c_char_p();ok=dll.stemtex_renderer_render_block(renderer,f['prepared'].encode(),f['width']*72.27/72,f['fontSize'],0,c.byref(r),c.byref(err),c.byref(m))
  assert ok and r.outcome == 0,(f['name'],err.value,m.value,r.message)
  out=base/(f['name']+'.pdf');shutil.copy2(r.path.decode(),out)
  page=PdfReader(out).pages[0];w,h=float(page.mediabox.width),float(page.mediabox.height)
  assert abs(w-f['width'])<.05,(f['name'],w)
  stream=page.get_contents().get_data()
  if f['name'].endswith('-red') and not f['name'].startswith('explicit-color-override'): assert b'1 0 0 rg' in stream,f['name']
  if 'explicit-color-override' in f['name']: assert b'0 0 1 rg' in stream,f['name']
  rows.append((f['name'],w,h));print(rows[-1],flush=True)
  dll.stemtex_renderer_free_output_result(c.byref(r));dll.stemtex_renderer_free_string(m)
 for color in ['black','red']:
  heights=[h for n,w,h in rows if n in [x+'-'+color for x in ['lowercase-a','uppercase-a','descender-g','inline-alpha']]]
  assert max(heights)-min(heights)<.02,heights
 print(f'PASS: {len(rows)} native block renders, fixed logical widths and consistent simple-line heights',flush=True)
finally:dll.stemtex_renderer_destroy(renderer)
