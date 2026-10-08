import json, subprocess, shlex, sys, os, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[3]/'build/native-release'
mode='asan'
if len(sys.argv)>1 and sys.argv[1]!='asan':raise SystemExit('Use the full CMake TSAN build described in the evidence document.')
out=Path(tempfile.mkdtemp(prefix='mirage-process-asan-'))
flags= ['-fsanitize='+('thread' if mode=='tsan' else 'address,undefined'),'-fno-omit-frame-pointer','-fno-sanitize-recover=all','-O1','-g0']
if mode!='tsan':flags+=['-fno-sanitize=vptr']
compiles=json.loads((root/'compile_commands.json').read_text())
# Instrument all own translation units in the selected target's dependency graph.
targets=['native_agent_integration_test'] if mode=='tsan' else ['native_chat_model_test','native_agent_integration_test','native_conversation_view_test']
compiled={}
for target in targets:
 cmds=subprocess.check_output(['ninja','-C',str(root),'-t','commands',target],text=True).splitlines()
 linkargs=shlex.split(cmds[-1])
 used_objects={arg for arg in linkargs if arg.endswith('.o')}
 for cmd in cmds:
  if '/usr/bin/ar qc ' not in cmd:continue
  ar=shlex.split(cmd);ar=ar[ar.index('/usr/bin/ar'):];ar=ar[:ar.index('&&')] if '&&' in ar else ar
  if ar[2] in linkargs:used_objects.update(ar[3:])
 objects={}
 for entry in compiles:
  args=shlex.split(entry['command'])
  obj=args[args.index('-o')+1]
  if '/third_party/' in entry['file'] or obj not in used_objects:continue
  dest=out/(obj.replace('/','_'))
  objects[obj]=str(dest)
  if obj in compiled:continue
  if dest.exists() and dest.stat().st_mtime >= Path(entry['file']).stat().st_mtime:
   compiled[obj]=True
   continue
  new=[];i=0
  while i<len(args):
   arg=args[i]
   if arg in ['-o','-MF','-MT']:i+=2;continue
   if arg=='-MD' or arg.startswith('-O'):i+=1;continue
   new.append(arg);i+=1
  subprocess.run(new+flags+['-o',str(dest)],cwd=entry['directory'],check=True)
  compiled[obj]=True
  print(mode,'instrumented',entry['file'],flush=True)
 # Replace instrumented own objects from archives as well as executable objects.
 archives=[]
 for cmd in cmds:
  if '/usr/bin/ar qc ' not in cmd:continue
  args=shlex.split(cmd);args=args[args.index('/usr/bin/ar'):];args=args[:args.index('&&')] if '&&' in args else args
  archive=args[2]
  if not any(obj in objects for obj in args[3:]):continue
  dest=out/(archive.replace('/','_'))
  if not dest.exists() or any(Path(objects[obj]).stat().st_mtime > dest.stat().st_mtime for obj in args[3:] if obj in objects):
   dest.unlink(missing_ok=True)
   subprocess.run(['/usr/bin/ar','qc',str(dest)]+[objects.get(obj,obj) for obj in args[3:]],cwd=root,check=True)
  archives.append((archive,str(dest)))
 args=shlex.split(cmds[-1]);args=args[args.index('/usr/bin/c++'):];args=args[:args.index('&&')] if '&&' in args else args
 replacement=dict(archives);replacement.update(objects)
 args=[replacement.get(arg,arg) for arg in args]
 executable=str(out/target);args[args.index('-o')+1]=executable;args+=flags
 subprocess.run(args,cwd=root,check=True)
 print('linked',executable,flush=True)
 env=dict(os.environ);env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1'
 subprocess.run((['setarch','x86_64','-R'] if mode=='tsan' else [])+[executable],cwd=root/'apps/native',env=env,check=True)
