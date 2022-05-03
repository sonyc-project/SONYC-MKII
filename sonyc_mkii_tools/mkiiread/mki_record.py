import sys
sys.path.append('/home/sonyc/sonycnode')

import os
from subprocess import Popen, PIPE
import time
import json
from sonycnode.capture import info, recorder
from sonycnode.utils import misc
from sonycnode.utils import sonyc_logger as loggerClass

parser = misc.load_settings()


def getUSBAudioId():
    dev_manager = info.devices()
    for ind, devices in enumerate(dev_manager.list_devices()):
        if devices[1] in json.loads(parser.get('record', 'sonyc_hw')):
            return ind


def cleanup_onfail():
    if r is not None and r._pa is not None:
        logger.error("USB microphone fail - shutting down pyaudio and cleaning up")
        r.stop_recorder()
        r.__del__()
        os.system('rm /tmp/*.wav')
        os.system('rm /tmp/*.flac')
        os.system('rm /tmp/*.csv')
        os.system('rm -rf /tmp/laeq_store_dir')
        os.system('find /mnt/sonycdata/ -name "*.csv" -type f -delete')
        exit()


if __name__ == '__main__':
    rec_interval = parser.getint('record', 'interval')
    coverage = parser.getfloat('record', 'coverage')
    out_dir = parser.get('record', 'out_dir').strip("'\"")
    rsa_key_path = parser.get('record', 'rsa_key').strip("'\"")
    logger = loggerClass.sonycLogger(loggername="async_record")
    if not (parser.has_option('vagrant', 'overide_no_lsusb') and
                parser.getboolean('vagrant', 'overide_no_lsusb')):
        mic_comm = 'lsusb | grep "Cypress\|JMTek"'
        process = Popen(mic_comm, stdout=PIPE, shell=True)
        (mic_resp, err) = process.communicate()

        if 'Device' not in mic_resp.strip():
            logger.critical('USB microphone fail on script start - exiting')
            exit()

    r = recorder.Record(index=getUSBAudioId(),
                        directory=out_dir,
                        rsa_key=rsa_key_path,
                        interval=rec_interval,
                        coverage=1, min_silence=0,
                        cal_val=74.52,
                        record=True)
    r.start_recorder()
    last_callback_update = 0
    curr_callback_update = 0
    while True:
        last_callback_update = r.check_callback()
        time.sleep(1)
        curr_callback_update = r.check_callback()
        if curr_callback_update - last_callback_update == 0:
            cleanup_onfail()