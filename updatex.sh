#!/bin/bash

# RUN THIS IF ALL THE FILES ARE FOR SOME REASON MARKED AS EXECUTABLE!!!!!!

find . -type f -exec chmod a-x {} +
chmod +x updatex.sh build.sh lines.sh AneoC/CTC.sh
cd AneoC
echo "CTC"
./CTC.sh
cd ..

