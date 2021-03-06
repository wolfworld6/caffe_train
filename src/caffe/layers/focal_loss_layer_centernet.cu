#include <algorithm>
#include <cfloat>
#include <vector>

#include "caffe/layers/focal_loss_layer_centernet.hpp"
#include "caffe/util/math_functions.hpp"

int count_iter = 0;
namespace caffe {

template <typename Dtype>
__global__ void focalSigmoidLossForwardGPU(const int nthreads,
          const Dtype* prob_data, const Dtype* label, Dtype* loss,
          const int batch, const int channels, const int height,
          const int width, Dtype* counts, float gamma, float alpha) {
  CUDA_KERNEL_LOOP(index, nthreads) {
    const int fw = (index / width / height) % width;
    const int fh = (index / width / height) / width;
    const int fc = (index / width / height) % channels;
    const int fn = (index / width / height) / channels;
    const int dim = (fn * channels + fc) * height * width;
    const Dtype* label_slice = label + dim;
    const Dtype* prob_slice = prob_data + dim;
    const Dtype label_a = label_slice[fh * width + fw];
    const Dtype prob_a = prob_slice[fh * width + fw];
    if( label_a == Dtype(1)){
      loss[index] -= log(max(prob_a, Dtype(FLT_MIN))) * powf(1 -prob_a, alpha);
      counts[index] = 1;
    }else if(label_a < Dtype(1)){
      loss[index] -= log(max(1 - prob_a, Dtype(FLT_MIN))) * powf(prob_a, alpha) *
                     powf(1 - label_a, gamma);
      counts[index] = 0;
    }
  }
}

template <typename Dtype>
void CenterNetfocalSigmoidWithLossLayer<Dtype>::Forward_gpu(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
    sigmoid_layer_->Forward(sigmoid_bottom_vec_, sigmoid_top_vec_);
    const Dtype* prob_data = prob_.gpu_data();
    const Dtype* label = bottom[1]->gpu_data();
    const int nthreads = prob_.count();
    batch_ = bottom[0]->num();
    num_class_ = bottom[0]->channels();
    width_ = bottom[0]->width();
    height_ = bottom[0]->height();
    Dtype* loss_data = bottom[0]->mutable_gpu_diff();
    Dtype* counts = prob_.mutable_gpu_diff();
    focalSigmoidLossForwardGPU<Dtype><<<CAFFE_GET_BLOCKS(nthreads),
        CAFFE_CUDA_NUM_THREADS>>>(nthreads, prob_data, label, loss_data,
        batch_, num_class_, height_, width_, counts, gamma_, alpha_);
    Dtype loss;
    caffe_gpu_asum(nthreads, loss_data, &loss);
    Dtype valid_count;
    caffe_gpu_asum(nthreads, counts, &valid_count);
    top[0]->mutable_cpu_data()[0] = loss / valid_count;
    if (top.size() == 2) {
      top[1]->ShareData(prob_);
    }
    if(count_iter % 1000 == 0)
      printf("\033[1m\033[45;33m batch_: %d, num_class_: %d, width: %d, height: %d, valid_count: %f, loss: %f, final_loss: %f \33[0m\n", 
                                batch_, num_class_, width_, height_, valid_count, loss, top[0]->mutable_cpu_data()[0]);
    count_iter++;
}

template <typename Dtype>
__global__ void focalSigmoidLossBackwardGPU(const int nthreads,
          const Dtype* label, const Dtype* prob_data, Dtype* bottom_diff, 
          const int batch, const int channels, const int height,
          const int width, Dtype* counts, float gamma, float alpha) {
  CUDA_KERNEL_LOOP(index, nthreads) {
    const int fw = (index / width / height) % width;
    const int fh = (index / width / height) / width;
    const int fc = (index / width / height) % channels;
    const int fn = (index / width / height) / channels;
    const int dim = (fn * channels + fc) * height * width;
    const Dtype* label_slice = label + dim;
    const Dtype* prob_slice = prob_data + dim;
    const Dtype label_a = label_slice[fh * width + fw];
    const Dtype prob_a = prob_slice[fh * width + fw];
    if(label_a == Dtype(1)){
      bottom_diff[index] = powf(1 - prob_a, alpha) * 
                                        (alpha * prob_a * log(max(prob_a, Dtype(FLT_MIN))) - (1 - prob_a));
      counts[index] = 1;
    }else if(label_a < Dtype(1)){
      bottom_diff[index] = powf(1 - label_a, gamma) * powf(prob_a, alpha) * 
                                        ( prob_a - alpha* (1 - prob_a) * log(max(1 - prob_a, Dtype(FLT_MIN))));
      counts[index] = 0;
    }
  }
}

template <typename Dtype>
void CenterNetfocalSigmoidWithLossLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  if (propagate_down[1]) {
    LOG(FATAL) << this->type()
                <<
                 " Layer cannot backpropagate to label inputs.";
  }
  if (propagate_down[0]) {
    Dtype* bottom_diff = bottom[0]->mutable_gpu_diff();
    const Dtype* prob_data = prob_.gpu_data();
    const Dtype* label = bottom[1]->gpu_data();
    const int nthreads = bottom[0]->count();
    Dtype* counts = prob_.mutable_gpu_diff();
    batch_ = bottom[0]->num();
    num_class_ = bottom[0]->channels();
    width_ = bottom[0]->width();
    height_ = bottom[0]->height();
    Dtype diff_sum;
    focalSigmoidLossBackwardGPU<Dtype><<<CAFFE_GET_BLOCKS(nthreads),
        CAFFE_CUDA_NUM_THREADS>>>(nthreads, label, prob_data, bottom_diff,
          batch_, num_class_, height_, width_, counts, gamma_, alpha_);
   
    Dtype valid_count;
    caffe_gpu_asum(nthreads, counts, &valid_count);
    const Dtype loss_weight = top[0]->cpu_diff()[0] / valid_count;
    caffe_gpu_scal(prob_.count(), loss_weight , bottom_diff);
    
    caffe_gpu_asum(nthreads, bottom_diff, &diff_sum);
    printf("\033[1m\033[45;33m cuda diff_sumarize_a: %f, count: %f  \33[0m\n", diff_sum, valid_count);
  
  }
}

INSTANTIATE_LAYER_GPU_FUNCS(CenterNetfocalSigmoidWithLossLayer);

}  // namespace caffe
