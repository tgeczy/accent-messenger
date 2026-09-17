"""Build and test SAPI without installing voices, opening audio, or using real settings."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    spec=importlib.util.spec_from_file_location('messenger_number_reference',ROOT/'nvda-addon/synthDrivers/messengerExperimental/numwords.py')
    reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)
    texts=['100','00000','00100','1,234.50','-12.5','21st','3rd','100th','0th','1000000000000000000',
           'v1.2.3','192.168.0.1','abc12','12abc','x 12. y','12,345','12,34','.5','1.2.','-10','hello E']
    rng=random.Random(401);texts += [str(rng.randrange(10**18)) for _ in range(100)]
    fixture=ROOT/'.build/number-parity.tsv'
    fixture.write_text('\n'.join(str(int(d))+'\t'+t+'\t'+reference.normalise(t,spell_out=d,lang='en')
                               for d in (False,True) for t in texts)+'\n')
    stage=ROOT/'dist/accent-messenger-sapi-0.4.0'
    reports={}
    for arch in ('x86','x64'):
        build=ROOT/('build-'+arch)
        with (ROOT/('.build/sapi-test-build-'+arch+'.log')).open('w') as log:
            subprocess.run(['cmake','-S',str(ROOT),'-B',str(build),'-DMESSENGER_BUILD_SAPI=ON'],stdout=log,stderr=subprocess.STDOUT,check=True)
            subprocess.run(['cmake','--build',str(build),'--config','MinSizeRel','--target','messenger','messenger_sapi','sapi_test','settings_test','--parallel','8'],stdout=log,stderr=subprocess.STDOUT,check=True)
        dll=stage/arch/'messenger_sapi.dll'
        shutil.copy2(build/'sapi/MinSizeRel/messenger_sapi.dll',dll)
        settings=ROOT/('.build/sapi-test-'+arch+'.toml')
        result=subprocess.run([str(build/'sapi/MinSizeRel/sapi_test.exe'),str(dll),str(settings),str(fixture)],capture_output=True,text=True,timeout=90)
        print(arch,result.stdout,result.stderr,flush=True)
        assert result.returncode==0,result.returncode
        dialog=subprocess.run([str(build/'sapi/MinSizeRel/settings_test.exe'),str(ROOT/('.build/sapi-gui-test-'+arch+'.toml'))],capture_output=True,text=True,timeout=20)
        print(dialog.stdout,dialog.stderr,flush=True)
        assert dialog.returncode==0,dialog.returncode
        pcm=Path(str(settings)+'.pcm').read_bytes()
        reports[arch]={'passed':True,'dll_sha256':hashlib.sha256(dll.read_bytes()).hexdigest(),
                       'lexical_cases':len(texts)*2,'com_test':result.stdout,'settings_test':dialog.stdout,
                       'E_pcm_sha256':hashlib.sha256(pcm).hexdigest(),'E_samples':len(pcm)//2}
    assert reports['x86']['E_pcm_sha256']==reports['x64']['E_pcm_sha256']
    (ROOT/'dist/sapi-validation.json').write_text(json.dumps({'architectures':reports,
        'audio_device_opened':False,'real_voice_registration_changed':False,'real_settings_changed':False},indent=2)+'\n')


if __name__=='__main__':main()
