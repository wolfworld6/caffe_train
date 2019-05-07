#!/bin/sh
if ! test -f ../prototxt/facelandmarknet/facelandmark_train_v9.prototxt ;then
	echo "error: ../prototxt/deepano_light_face_train.prototxt does not exit."
	echo "please generate your own model prototxt primarily."
        exit 1
fi
if ! test -f ../prototxt/facelandmarknet/facelandmark_test_v9.prototxt ;then
	echo "error: ../prototxt/deepano_light_face_train.prototxt does not exit."
	echo "please generate your own model prototxt primarily."
        exit 1
fi
../../../../build/tools/caffe train --solver=../solver/facelandmark_solver_train_v9.prototxt -gpu 0 \
#--snapshot=../snapshot/deepanoFacelandmark_v9_iter_54680.solverstate
