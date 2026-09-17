"""Build/stage the native SAPI adapters and settings tool; never register or install."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
VERSION='0.4.0'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage-only',action='store_true')
    args=parser.parse_args()
    driver=ROOT/'assets/SPKMIC.TSR'
    fit=ROOT/'assets/calibration.json'
    digest=hashlib.sha256(driver.read_bytes()).hexdigest()
    assert digest=='a2963b7025b67c577e3e3839342d9f49c39c7039d0ffcad36b1f066c816a1572'
    params=json.loads(fit.read_text())['models']['constant']['parameters']
    (ROOT/'.build/sapi_calibration.h').write_text('#pragma once\nstatic const double calibration[]={'+','.join(repr(p) for p in params)+'};\nstatic const unsigned char driverDigest[]={'+','.join('0x'+digest[i:i+2] for i in range(0,64,2))+'};\n')
    stage=ROOT/'dist'/('accent-messenger-sapi-'+VERSION)
    stage.mkdir(parents=True,exist_ok=True)
    for arch,target in (('x86','Win32'),('x64','x64')):
        if not args.stage_only:
            subprocess.run(['cmake','-S',str(ROOT),'-B',str(ROOT/('build-'+arch)),'-G','Visual Studio 17 2022','-A',target,'-DMESSENGER_BUILD_SAPI=ON'],check=True)
            subprocess.run(['cmake','--build',str(ROOT/('build-'+arch)),'--config','MinSizeRel','--target','messenger','messenger_sapi','messenger_settings','--parallel','8'],check=True)
        (stage/arch).mkdir(exist_ok=True)
        shutil.copy2(ROOT/('build-'+arch)/'sapi/MinSizeRel/messenger_sapi.dll',stage/arch/'messenger_sapi.dll')
    shutil.copy2(ROOT/'build-x86/sapi/MinSizeRel/messenger_settings.exe',stage/'messenger_settings.exe')
    (stage/'data').mkdir(exist_ok=True);shutil.copy2(driver,stage/'data/SPKMIC.TSR')
    for name in ('LICENSE','THIRD-PARTY-NOTICES.md'):shutil.copy2(ROOT/name,stage/name)
    shutil.copy2(ROOT/'docs/sapi.md',stage/'README.md')
    (stage/'licenses').mkdir(exist_ok=True)
    for source,name in [('NUMPY-LICENSE.txt','NUMPY-LICENSE.txt'),('PCG-LICENSE.md','PCG-LICENSE.md'),('YY-Thunks/LICENSE','YY-Thunks-LICENSE.txt')]:
        shutil.copy2(ROOT/'native/third_party'/source,stage/'licenses'/name)
    compiler=Path('C:/Program Files (x86)/Inno Setup 6/ISCC.exe')
    if compiler.exists():subprocess.run([str(compiler),'/DStageDir='+str(stage),str(ROOT/'sapi/installer.iss')],check=True)
    else:print('Inno Setup is unavailable; staged files are ready but installer is not built.')
    print(stage)


if __name__=='__main__':main()
