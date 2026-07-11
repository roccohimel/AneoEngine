#!/bin/bash

# RUN THIS IF ALL THE FILES ARE FOR SOMEREASON MARKED AS EXECUTABLE!!!!!!

find . -type f -exec chmod a-x {} +
chmod +x updatex.sh build.sh lines.sh AneoC/CTC.sh
cd AneoC
echo "WAIT!!! Compiling the compiler..."
./CTC.sh
cd ..

