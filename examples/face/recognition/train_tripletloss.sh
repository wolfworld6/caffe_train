python3 src/train_tripletloss_copy.py \
<<<<<<< HEAD
--logs_base_dir ./logs/ \
--models_base_dir ./models/  \
--data_dir ../../../../dataset/facedata/recognition/lfw/lfw_160 \
=======
--logs_base_dir ./train/logs/ \
--models_base_dir ./train/models/  \
--data_dir ../../../../dataset/facedata/lfw/lfw_160 \
>>>>>>> 18d975d2229705554e3e2896ca479bed6a135dc4
--model_def models.inception_resnet_v1 \
--optimizer RMSPROP \
--image_size 160 \
--batch_size 30 \
--learning_rate_schedule_file data/learning_rate_schedule_classifier_casia.txt \
--learning_rate 0.01 \
--weight_decay 1e-4 \
--max_nrof_epochs 50000 \
--epoch_size 50000 \
--gpu_memory_fraction 0.7
