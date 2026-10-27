# SPDX-License-Identifier: GPL-2.0-or-later
import ctypes as c, pathlib, json, shutil, re
from pypdf import PdfReader
from pypdf.generic import ContentStream
import argparse
parser=argparse.ArgumentParser(description="Opt-in native StemTeX block geometry regression; requires pypdf.")
for name in ['runtime', 'profile', 'texmf', 'fixtures', 'output']: parser.add_argument('--'+name, required=True)
parser.add_argument('--basic', action='store_true', help='Test the bundled profile without CJK, xcolor, or amsmath environments.')
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
worker=base/'worker.tex'
setup=base/'request-setup.tex'
setup.write_text('',encoding='utf-8')
worker.write_text((pathlib.Path(__file__).resolve().parents[1]/'part/latexworker.tex').read_text(encoding='utf-8').replace('@@MENGSHEE_REQUEST_SETUP@@',setup.as_posix()),encoding='utf-8')
config=Config(str(root/'runtime').encode(),str(root/'runtime').encode(),str(pathlib.Path(args.texmf).resolve()).encode(),str(pathlib.Path(args.profile).resolve()).encode(),str((base/'state').resolve()).encode(),str((base/'renders').resolve()).encode(),90000,90000,0,0,0,0,1,0,str(worker).encode())
err=c.c_int();msg=c.c_char_p();renderer=dll.stemtex_renderer_create(c.byref(config),c.byref(err),c.byref(msg));assert renderer,msg.value
# Compare with the native SDK given the same application display spacing.
display_spacing = ''.join('\\setlength{\\'+name+'}{0pt}%\n' for name in ('abovedisplayskip','belowdisplayskip','abovedisplayshortskip','belowdisplayshortskip'))
reference_config=Config(str(root/'runtime').encode(),str(root/'runtime').encode(),str(pathlib.Path(args.texmf).resolve()).encode(),str(pathlib.Path(args.profile).resolve()).encode(),str((base/'reference-state').resolve()).encode(),str((base/'reference-renders').resolve()).encode(),90000,90000,0,0,0,0,1,0,None)
reference=dll.stemtex_renderer_create(c.byref(reference_config),c.byref(err),c.byref(msg));assert reference,msg.value
fixtures=json.loads(pathlib.Path(args.fixtures).read_text(encoding='utf-8'))
selected=['lowercase-a','uppercase-a','descender-g','inline-alpha','inline-tall-fraction','multiple-paragraphs','pure-display-plus','pure-display-descender','pure-display-alpha','pure-display-projection','pure-display-fraction','bracket-display','equation','multiline-align','multiline-gather','alpha-followed-by-text','trailing-comment','trailing-blanklines','display-current-colorlet','display-explicit-color-override','text-current-color','explicit-color-override','text-grouped-blue']
chosen=[f for f in fixtures if any(f['name']==n+'-'+co for n in selected for co in ['black','red'])]
paragraph='这个投影算符的“像”（image），即所有可能的投影结果组成的集合：\n'+r'$\operatorname{im}P(q_0) =\{P(q_0)v:\ v\in\mathcal H_e\}.$'
if args.basic:
 chosen=[f for f in chosen if not any(token in f['name'] for token in ['color','blue','multiline'])]
else:
 chosen.append(dict(name='chinese',prepared=paragraph,setup=fixtures[0]['setup'],width=300,fontSize=10))
if not args.basic:
 split_source=r"""\begin{equation*}
\begin{split}
\sum_{A} &\boldsymbol{X}_{A} \times \frac{\partial E_{P S}}{\partial \boldsymbol{X}_{A}}+ \sum_{A} \boldsymbol{P}_{A} \times \frac{\partial E_{P S}}{\partial \boldsymbol{P}_{A}}
\\
 &=\sum_{A} \boldsymbol{X}_{A} \times\left\langle\Psi_{\mathrm{PS}}\right| \frac{\partial \hat{H}_{P S}}{\partial \boldsymbol{X}_{A}}\left|\Psi_{\mathrm{PS}}\right\rangle+\sum_{A} \boldsymbol{P}_{A} \times\left\langle\Psi_{\mathrm{PS}}\right| \frac{\partial \hat{H}_{P S}}{\partial \boldsymbol{P}_{A}}\left|\Psi_{\mathrm{PS}}\right\rangle .
\end{split}
\end{equation*}"""
 for colour in ['black','red']:
  setting=next(f['setup'] for f in fixtures if f['name']=='lowercase-a-'+colour)
  chosen.append(dict(name='equation-split-user-'+colour,prepared=split_source,setup=setting,width=500,fontSize=10))
rows=[]
try:
 for f in chosen:
  setup.write_text(f['setup'],encoding='utf-8')
  r=Result();m=c.c_char_p();ok=dll.stemtex_renderer_render_block(renderer,f['prepared'].encode(),f['width']*72.27/72,f['fontSize'],0,c.byref(r),c.byref(err),c.byref(m))
  assert ok and r.outcome == 0,(f['name'],err.value,m.value,r.message)
  out=base/(f['name']+'.pdf');shutil.copy2(r.path.decode(),out)
  page=PdfReader(out).pages[0];w,h=float(page.mediabox.width),float(page.mediabox.height)
  reference_result=Result();reference_message=c.c_char_p()
  ok=dll.stemtex_renderer_render_block(reference,(display_spacing+f['prepared']).encode(),f['width']*72.27/72,f['fontSize'],0,c.byref(reference_result),c.byref(err),c.byref(reference_message))
  assert ok and reference_result.outcome==0,(f['name'],reference_message.value)
  reference_page=PdfReader(reference_result.path.decode()).pages[0]
  expected_size=(float(reference_page.mediabox.width),float(reference_page.mediabox.height))
  assert abs(w-expected_size[0])<.02 and abs(h-expected_size[1])<.02,(f['name'],(w,h),expected_size)
  # Compare text origins as well: equal page size alone could hide a shifted top.
  def origins(p):
   result=[]
   p.extract_text(visitor_text=lambda text,cm,tm,font,size: result.append((text,tuple(round(float(v),3) for v in cm),tuple(round(float(v),3) for v in tm),round(float(size),3))) if text.strip() else None)
   return result
  assert origins(page)==origins(reference_page),(f['name'],origins(page),origins(reference_page))
  dll.stemtex_renderer_free_output_result(c.byref(reference_result));dll.stemtex_renderer_free_string(reference_message)
  fill=(0.,0.,0.);stack=[];painted=[]
  for operands,op in ContentStream(page.get_contents(),page.pdf).operations:
   if op==b'q': stack.append(fill)
   elif op==b'Q': fill=stack.pop()
   elif op==b'rg': fill=tuple(float(v) for v in operands)
   elif op==b'g': fill=(float(operands[0]),)*3
   elif op in (b'Tj',b'TJ',b"'",b'"'): painted.append(fill)
  expected=(1.,0.,0.) if f['name'].endswith('-red') else (0.,0.,0.)
  assert painted,f['name']
  if f['name'].startswith('display-explicit-color-override'):
   assert painted[0]==(0.,0.,1.) and painted[-1]==expected,(f['name'],painted)
  elif 'explicit-color-override' in f['name']:
   assert all(v==(0.,0.,1.) for v in painted),(f['name'],painted)
  elif 'text-grouped-blue' in f['name']:
   assert painted[0]==expected and painted[-1]==expected and (0.,0.,1.) in painted,(f['name'],painted)
  else:
   assert all(v==expected for v in painted),(f['name'],painted)
  rows.append((f['name'],w,h));print(rows[-1],flush=True)
  dll.stemtex_renderer_free_output_result(c.byref(r));dll.stemtex_renderer_free_string(m)
 for color in ['black','red']:
  heights=[h for n,w,h in rows if n in [x+'-'+color for x in ['lowercase-a','uppercase-a','descender-g','inline-alpha']]]
  assert max(heights)-min(heights)<.02,heights
 heights_by_name={n:h for n,w,h in rows}
 for color in ['black','red']:
  assert abs(heights_by_name['pure-display-alpha-'+color]-heights_by_name['bracket-display-'+color]) < .03
  assert abs(heights_by_name['alpha-followed-by-text-'+color]-heights_by_name['trailing-blanklines-'+color]) < .03
 for n,w,h in rows:
  if n.endswith('-red'):
   assert abs(h-heights_by_name[n[:-4]+'-black']) < .03,(n,h)
 print(f'PASS: {len(rows)} native block renders, SDK-identical page bounds and text positions; colours and line heights verified',flush=True)
finally:
 dll.stemtex_renderer_destroy(renderer)
 dll.stemtex_renderer_destroy(reference)
