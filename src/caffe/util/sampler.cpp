#include <algorithm>
#include <vector>

#include "caffe/util/bbox_util.hpp"
#include "caffe/util/sampler.hpp"
#include "caffe/util/im_transforms.hpp"
#include "caffe/util/io.hpp"
#include "google/protobuf/repeated_field.h"
using google::protobuf::RepeatedPtrField;

#define COMPAREMIN(a, b) (a >= b ? b : a)
#define COMPAREMAX(a, b) (a >= b ? a : b)

namespace caffe {

void GenerateJitterSamples(const AnnotatedDatum& anno_datum, float jitter, vector<NormalizedBBox>* sampled_bboxes)
{
	float img_w,img_h,off_x,off_y;
  vector<NormalizedBBox> object_bboxes;
  GroupObjectBBoxes(anno_datum, &object_bboxes);

	caffe_rng_uniform(1, 1.0f - jitter, 1.0f, &img_w);
	caffe_rng_uniform(1, 1.0f - jitter, 1.0f, &img_h);
	caffe_rng_uniform(1, 0.0f, 1.0f - img_w, &off_x);
	caffe_rng_uniform(1, 0.0f, 1.0f - img_h, &off_y);

	NormalizedBBox sampled_bbox;
  SampleConstraint min_object_coverage_Constraint;
  min_object_coverage_Constraint.set_min_object_coverage(0.85);
  if(SatisfySampleConstraint(sampled_bbox, object_bboxes, min_object_coverage_Constraint)){
    sampled_bbox.set_xmin(off_x);
    sampled_bbox.set_ymin(off_y);
    sampled_bbox.set_xmax(off_x + img_w);
    sampled_bbox.set_ymax(off_y + img_h);
  }else{
    sampled_bbox.set_xmin(0.f);
    sampled_bbox.set_ymin(0.f);
    sampled_bbox.set_xmax(1.f);
    sampled_bbox.set_ymax(1.f);
  }
	sampled_bboxes->push_back(sampled_bbox);

}

void GroupObjectBBoxes(const AnnotatedDatum& anno_datum,
                       vector<NormalizedBBox>* object_bboxes) {
  object_bboxes->clear();
  for (int i = 0; i < anno_datum.annotation_group_size(); ++i) {
    const AnnotationGroup& anno_group = anno_datum.annotation_group(i);
    for (int j = 0; j < anno_group.annotation_size(); ++j) {
      const Annotation& anno = anno_group.annotation(j);
      object_bboxes->push_back(anno.bbox());
    }
  }
}

bool SatisfySampleConstraint(const NormalizedBBox& sampled_bbox,
                             const vector<NormalizedBBox>& object_bboxes,
                             const SampleConstraint& sample_constraint) {
  bool has_jaccard_overlap = sample_constraint.has_min_jaccard_overlap() ||
      sample_constraint.has_max_jaccard_overlap();
  bool has_sample_coverage = sample_constraint.has_min_sample_coverage() ||
      sample_constraint.has_max_sample_coverage();
  bool has_object_coverage = sample_constraint.has_min_object_coverage() ||
      sample_constraint.has_max_object_coverage();
  bool satisfy = !has_jaccard_overlap && !has_sample_coverage &&
      !has_object_coverage;
  if (satisfy) {
    // By default, the sampled_bbox is "positive" if no constraints are defined.
    return true;
  }
  // Check constraints.
  bool found = false;
  for (int i = 0; i < object_bboxes.size(); ++i) {
    const NormalizedBBox& object_bbox = object_bboxes[i];
    // Test jaccard overlap.
    if (has_jaccard_overlap) {
      const float jaccard_overlap = JaccardOverlap(sampled_bbox, object_bbox);
      if (sample_constraint.has_min_jaccard_overlap() &&
          jaccard_overlap < sample_constraint.min_jaccard_overlap()) {
        continue;
      }
      if (sample_constraint.has_max_jaccard_overlap() &&
          jaccard_overlap > sample_constraint.max_jaccard_overlap()) {
        continue;
      }
      found = true;
    }
    // Test sample coverage.
    if (has_sample_coverage) {
      const float sample_coverage = BBoxCoverage(sampled_bbox, object_bbox);
      if (sample_constraint.has_min_sample_coverage() &&
          sample_coverage < sample_constraint.min_sample_coverage()) {
        continue;
      }
      if (sample_constraint.has_max_sample_coverage() &&
          sample_coverage > sample_constraint.max_sample_coverage()) {
        continue;
      }
      found = true;
    }
    // Test object coverage.
    if (has_object_coverage) {
      const float object_coverage = BBoxCoverage(object_bbox, sampled_bbox);
      if (sample_constraint.has_min_object_coverage() &&
          object_coverage < sample_constraint.min_object_coverage()) {
        continue;
      }
      if (sample_constraint.has_max_object_coverage() &&
          object_coverage > sample_constraint.max_object_coverage()) {
        continue;
      }
      found = true;
    }
    if (found) {
      return true;
    }
  }
  return found;
}

void SampleBBox(const Sampler& sampler, NormalizedBBox* sampled_bbox, float orl_ratio) {
  // Get random scale.
  CHECK_GE(sampler.max_scale(), sampler.min_scale());
  CHECK_GT(sampler.min_scale(), 0.);
  CHECK_LE(sampler.max_scale(), 1.);
  float scale;

  caffe_rng_uniform(1, sampler.min_scale(), sampler.max_scale(), &scale);

  // Get random aspect ratio.
  CHECK_GE(sampler.max_aspect_ratio(), sampler.min_aspect_ratio());
  CHECK_GT(sampler.min_aspect_ratio(), 0.);
  CHECK_LT(sampler.max_aspect_ratio(), FLT_MAX);
  float aspect_ratio;
  caffe_rng_uniform(1, sampler.min_aspect_ratio(), sampler.max_aspect_ratio(),
      &aspect_ratio);

  aspect_ratio = std::max<float>(aspect_ratio, std::pow(scale, 2.));
  aspect_ratio = std::min<float>(aspect_ratio, 1 / std::pow(scale, 2.));

  // Figure out bbox dimension.
  float bbox_width = scale * sqrt(aspect_ratio);
  float bbox_height = scale / sqrt(aspect_ratio);

  // Figure out top left coordinates.
  float w_off, h_off;
  caffe_rng_uniform(1, 0.f, 1 - bbox_width, &w_off);
  caffe_rng_uniform(1, 0.f, 1 - bbox_height, &h_off);

  sampled_bbox->set_xmin(w_off);
  sampled_bbox->set_ymin(h_off);
  sampled_bbox->set_xmax(w_off + bbox_width);
  sampled_bbox->set_ymax(h_off + bbox_height);
}

void SampleBBox_Square(const AnnotatedDatum& anno_datum, const Sampler& sampler, NormalizedBBox* sampled_bbox) {
  // Get random scale.
  CHECK_GE(sampler.max_scale(), sampler.min_scale());
  CHECK_GT(sampler.min_scale(), 0.);
  CHECK_LE(sampler.max_scale(), 1.);

  const Datum datum = anno_datum.datum();

  int datum_height = datum.height();
  int datum_width = datum.width();
  int min_side = datum_height;
  float min_side_scale = 0.0;

  min_side = datum_height > datum_width ? datum_width : datum_height;

  // printf("height=%d, width=%d, min_side=%d\n", datum_height, datum_width, min_side);

  float scale;
  caffe_rng_uniform(1, sampler.min_scale(), sampler.max_scale(), &scale);
  min_side_scale = min_side * scale;

  // printf("scale=%f, min_side_scale = %f\n", scale, min_side_scale);

  float bbox_width = min_side_scale/datum_width;
  float bbox_height = min_side_scale/datum_height;
  // printf("bbox_width=%f, bbox_height=%f\n", bbox_width, bbox_height);


  // Figure out top left coordinates.
  float w_off, h_off;
  caffe_rng_uniform(1, 0.f, 1 - bbox_width, &w_off);
  caffe_rng_uniform(1, 0.f, 1 - bbox_height, &h_off);

  sampled_bbox->set_xmin(w_off);
  sampled_bbox->set_ymin(h_off);
  sampled_bbox->set_xmax(w_off + bbox_width);
  sampled_bbox->set_ymax(h_off + bbox_height);
}



void GenerateSamples(const NormalizedBBox& source_bbox,
                     const vector<NormalizedBBox>& object_bboxes,
                     const BatchSampler& batch_sampler,
                     vector<NormalizedBBox>* sampled_bboxes, float orl_ratio) {
  int found = 0;
  for (int i = 0; i < batch_sampler.max_trials(); ++i) {
    if (batch_sampler.has_max_sample() &&
        found >= batch_sampler.max_sample()) {
      break;
    }
    // Generate sampled_bbox in the normalized space [0, 1].
    NormalizedBBox sampled_bbox;
    SampleBBox(batch_sampler.sampler(), &sampled_bbox, orl_ratio);
    // Transform the sampled_bbox w.r.t. source_bbox.
    LocateBBox(source_bbox, sampled_bbox, &sampled_bbox);
    // Determine if the sampled bbox is positive or negative by the constraint.
    if (SatisfySampleConstraint(sampled_bbox, object_bboxes,
                                batch_sampler.sample_constraint())) {
      ++found;
      sampled_bboxes->push_back(sampled_bbox);
    }
  }
}

void GenerateSamples_Square(const AnnotatedDatum& anno_datum,
                     const NormalizedBBox& source_bbox,
                     const vector<NormalizedBBox>& object_bboxes,
                     const BatchSampler& batch_sampler,
                     vector<NormalizedBBox>* sampled_bboxes) {
  int found = 0;
  for (int i = 0; i < batch_sampler.max_trials(); ++i) {
    if (batch_sampler.has_max_sample() &&
        found >= batch_sampler.max_sample()) {
      break;
    }
    // Generate sampled_bbox in the normalized space [0, 1].
    NormalizedBBox sampled_bbox;
    SampleBBox_Square(anno_datum, batch_sampler.sampler(), &sampled_bbox);
    // Transform the sampled_bbox w.r.t. source_bbox.
    LocateBBox(source_bbox, sampled_bbox, &sampled_bbox);
    // Determine if the sampled bbox is positive or negative by the constraint.
    if (SatisfySampleConstraint(sampled_bbox, object_bboxes,
                                batch_sampler.sample_constraint())) {
      ++found;
      sampled_bboxes->push_back(sampled_bbox);
    }
  }
}


void GenerateBatchSamples(const AnnotatedDatum& anno_datum,
                          const vector<BatchSampler>& batch_samplers,
                          vector<NormalizedBBox>* sampled_bboxes) {
  sampled_bboxes->clear();
  vector<NormalizedBBox> object_bboxes;
  GroupObjectBBoxes(anno_datum, &object_bboxes);
  const int img_height = anno_datum.datum().height();
  const int img_width = anno_datum.datum().width();
  float ratio = (float)img_height / img_width;
  for (int i = 0; i < batch_samplers.size(); ++i) {
    if (batch_samplers[i].use_original_image()) {
      NormalizedBBox unit_bbox;
      unit_bbox.set_xmin(0);
      unit_bbox.set_ymin(0);
      unit_bbox.set_xmax(1);
      unit_bbox.set_ymax(1);
      GenerateSamples(unit_bbox, object_bboxes, batch_samplers[i],
                      sampled_bboxes, ratio);
    }
  }
}

void GenerateBatchSamples_Square(const AnnotatedDatum& anno_datum,
                          const vector<BatchSampler>& batch_samplers,
                          vector<NormalizedBBox>* sampled_bboxes) {
  sampled_bboxes->clear();
  vector<NormalizedBBox> object_bboxes;
  GroupObjectBBoxes(anno_datum, &object_bboxes);
  for (int i = 0; i < batch_samplers.size(); ++i) {
    if (batch_samplers[i].use_original_image()) {
      NormalizedBBox unit_bbox;
      unit_bbox.set_xmin(0);
      unit_bbox.set_ymin(0);
      unit_bbox.set_xmax(1);
      unit_bbox.set_ymax(1);
      GenerateSamples_Square(anno_datum, unit_bbox, object_bboxes, batch_samplers[i],
                      sampled_bboxes);
    }
  }
}

void GenerateDataAnchorSample(const AnnotatedDatum& anno_datum, 
                                const DataAnchorSampler& data_anchor_sampler,
                                const vector<NormalizedBBox>& object_bboxes,
                                int resized_height, int resized_width,
                                NormalizedBBox* sampled_bbox, AnnotatedDatum* resized_anno_datum,
                                const TransformationParameter& trans_param, bool do_resize){
  vector<int>anchorScale;
  int img_height = anno_datum.datum().height();
  int img_width = anno_datum.datum().width();
  anchorScale.clear();
  for(int s = 0 ; s < data_anchor_sampler.scale_size(); s++){
    anchorScale.push_back(data_anchor_sampler.scale(s));
  }
  CHECK_GT(object_bboxes.size(), 0);
  int object_bbox_index = caffe_rng_rand() % object_bboxes.size();
  const float xmin = object_bboxes[object_bbox_index].xmin()*img_width;
  const float xmax = object_bboxes[object_bbox_index].xmax()*img_width;
  const float ymin = object_bboxes[object_bbox_index].ymin()*img_height;
  const float ymax = object_bboxes[object_bbox_index].ymax()*img_height;
  float bbox_width = xmax - xmin;
  float bbox_height = ymax - ymin;
  int range_size = 0, range_idx_size = 0, rng_random_index = 0; 
  float bbox_aera = bbox_height * bbox_width;
  float scaleChoose = 0.0f; 
  float min_resize_val = 0.f, max_resize_val = 0.f;
  for(int j = 0; j < anchorScale.size() -1; ++j){
    if(bbox_aera >= std::pow(anchorScale[j], 2) && bbox_aera < std::pow(anchorScale[j+1], 2)){
      range_size = j + 1;
      break;
    }
  }
  if(bbox_aera > std::pow(anchorScale[anchorScale.size() - 2], 2))
    range_size = anchorScale.size() - 2;
  if(range_size==0){
    range_idx_size = 0;
  }else{
    rng_random_index = caffe_rng_rand() % (range_size + 1);
    range_idx_size = rng_random_index % (range_size + 1);
  }
  if(range_idx_size == range_size){
    min_resize_val = anchorScale[range_idx_size] / 2;
    max_resize_val = COMPAREMIN((float)anchorScale[range_idx_size] * 2,
                                                  2*std::sqrt(bbox_aera));
    if(min_resize_val <= max_resize_val)
      caffe_rng_uniform(1, min_resize_val, max_resize_val, &scaleChoose);
    else
      caffe_rng_uniform(1, max_resize_val, min_resize_val, &scaleChoose);
  }else{
    min_resize_val = anchorScale[range_idx_size] / 2;
    max_resize_val = (float)anchorScale[range_idx_size] * 2;
    caffe_rng_uniform(1, min_resize_val, max_resize_val, &scaleChoose);
  }
   
  float width_offset_org = 0.0f, height_offset_org = 0.0f;
  float w_off = 0.0f, h_off = 0.0f, w_end = 0.0f, h_end = 0.0f;
  if(do_resize){
    float scale = (float) scaleChoose / bbox_width;
    ResizedCropSample(anno_datum, resized_anno_datum, scale, trans_param);
    int Resized_ori_Height = int(scale * img_height);
    int Resized_ori_Width = int(scale * img_width);
    int Resized_bbox_width = int(scale * bbox_width);
    int Resized_bbox_height = int(scale * bbox_height);
    const float Resized_xmin = object_bboxes[object_bbox_index].xmin() * Resized_ori_Width;
    const float Resized_ymin = object_bboxes[object_bbox_index].ymin() * Resized_ori_Height;
    if(resized_width < std::max(Resized_ori_Height, Resized_ori_Width)){
      if(Resized_bbox_width <= resized_width){
        if(Resized_bbox_width == resized_width){
          width_offset_org = xmin;
        }else if(Resized_bbox_width < resized_width){
          caffe_rng_uniform(1, Resized_bbox_width + Resized_xmin - resized_width, Resized_xmin, &width_offset_org );
        }
      }else{
        caffe_rng_uniform(1, Resized_xmin, Resized_bbox_width + Resized_xmin - resized_width, &width_offset_org);
      }
      if(Resized_bbox_height <= resized_height){
        if(Resized_bbox_height == resized_height){
          height_offset_org = ymin;
        }else if(Resized_bbox_height < resized_height){
          caffe_rng_uniform(1, Resized_ymin + Resized_bbox_height - resized_height, Resized_ymin, &height_offset_org);
        }
      }else{
        caffe_rng_uniform(1, Resized_ymin, Resized_ymin + Resized_bbox_height - resized_height, &height_offset_org);
      }
    }else{
      caffe_rng_uniform(1, float(Resized_ori_Width-resized_width), 0.0f, &width_offset_org);
      caffe_rng_uniform(1, float(Resized_ori_Height-resized_height), 0.0f, &height_offset_org);
    }
    int width_offset_ = std::floor(width_offset_org);
    int height_offset_ = std::floor(height_offset_org);
    w_off = (float) width_offset_ / Resized_ori_Width;
    h_off = (float) height_offset_ / Resized_ori_Height;
    w_end = w_off + float(resized_width / Resized_ori_Width);
    h_end = h_off + float(resized_height / Resized_ori_Height);
    sampled_bbox->set_xmin(w_off);
    sampled_bbox->set_ymin(h_off);
    sampled_bbox->set_xmax(w_end);
    sampled_bbox->set_ymax(h_end);
  }else{
    resized_anno_datum->CopyFrom(anno_datum);
    sampled_bbox->set_xmin(0.f);
    sampled_bbox->set_ymin(0.f);
    sampled_bbox->set_xmax(1.f);
    sampled_bbox->set_ymax(1.f);
  }
}

void GenerateBatchDataAnchorSamples(const AnnotatedDatum& anno_datum,
                                const vector<DataAnchorSampler>& data_anchor_samplers,
                                int resized_height, int resized_width, 
                                NormalizedBBox* sampled_bbox, AnnotatedDatum* resized_anno_datum, 
                                const TransformationParameter& trans_param, bool do_resize) {
  CHECK_EQ(data_anchor_samplers.size(), 1);
  vector<NormalizedBBox> object_bboxes;
  GroupObjectBBoxes(anno_datum, &object_bboxes);
  for (int i = 0; i < data_anchor_samplers.size(); ++i) {
    if (data_anchor_samplers[i].use_original_image()) {
      int found = 0;
      for (int j = 0; j < data_anchor_samplers[i].max_trials(); ++j) {
        if (data_anchor_samplers[i].has_max_sample() &&
            found >= data_anchor_samplers[i].max_sample()) {
          break;
        }
        AnnotatedDatum temp_anno_datum;
        NormalizedBBox temp_sampled_bbox;
        GenerateDataAnchorSample(anno_datum, data_anchor_samplers[i], object_bboxes, resized_height, 
                                resized_width, &temp_sampled_bbox, &temp_anno_datum, trans_param, do_resize);
        if (SatisfySampleConstraint(temp_sampled_bbox, object_bboxes,
                                      data_anchor_samplers[i].sample_constraint())){
          found++;
          resized_anno_datum->CopyFrom(temp_anno_datum);
          sampled_bbox->CopyFrom(temp_sampled_bbox);
        }
      }
      if(found == 0){
        resized_anno_datum->CopyFrom(anno_datum);
        sampled_bbox->set_xmin(0.f);
        sampled_bbox->set_ymin(0.f);
        sampled_bbox->set_xmax(1.f);
        sampled_bbox->set_ymax(1.f);
      }
    }else{
      LOG(FATAL)<<"must use original_image";
    }
  }
  CHECK_GT(resized_anno_datum->datum().channels(), 0)<<"channels: "<<resized_anno_datum->datum().channels();
}

void GenerateLffdSample(const AnnotatedDatum& anno_datum,
                        int resized_height, int resized_width,
                        NormalizedBBox* sampled_bbox, 
                        std::vector<int> bbox_small_size_list,
                        std::vector<int> bbox_large_size_list,
                        std::vector<int> anchorStride, 
                        AnnotatedDatum* resized_anno_datum, 
                        const TransformationParameter& trans_param,
                        bool do_resize){
  CHECK_EQ(bbox_large_size_list.size(), bbox_small_size_list.size());
  vector<NormalizedBBox> object_bboxes;
  GroupObjectBBoxes(anno_datum, &object_bboxes);
  int num_output_scale = bbox_small_size_list.size();
  int img_height = anno_datum.datum().height();
  int img_width = anno_datum.datum().width();
  CHECK_GT(object_bboxes.size(), 0);
  int object_bbox_index = caffe_rng_rand() % object_bboxes.size();
  const float xmin = object_bboxes[object_bbox_index].xmin()*img_width;
  const float xmax = object_bboxes[object_bbox_index].xmax()*img_width;
  const float ymin = object_bboxes[object_bbox_index].ymin()*img_height;
  const float ymax = object_bboxes[object_bbox_index].ymax()*img_height;
  float bbox_width = xmax - xmin;
  float bbox_height = ymax - ymin;
  float longer_side = COMPAREMAX(bbox_height, bbox_width);
  int scaled_idx = 0, side_length = 0;
  if(longer_side <= bbox_small_size_list[0]){
    scaled_idx = 0;
  }else if(longer_side <= bbox_small_size_list[2]){
    scaled_idx = caffe_rng_rand() % 3;
  }else if(longer_side >= bbox_small_size_list[num_output_scale - 1]){
    scaled_idx = num_output_scale - 1;
  }else{
    for(int ii = 3; ii < num_output_scale - 1; ii++){
      if(longer_side >= bbox_small_size_list[ii] && longer_side < bbox_small_size_list[ii + 1])
        scaled_idx = ii;
    }
  }
  if(scaled_idx == (num_output_scale - 1)){
    side_length = bbox_large_size_list[num_output_scale - 1] 
                    + caffe_rng_rand() % (static_cast<int>(bbox_large_size_list[num_output_scale - 1] * 0.5));
  }else{
    side_length = bbox_small_size_list[scaled_idx] 
                    + caffe_rng_rand() % (bbox_large_size_list[scaled_idx] - 
                                          bbox_small_size_list[scaled_idx]);
  }
  if(do_resize){
    float scale = (float) side_length / bbox_width;
    ResizedCropSample(anno_datum, resized_anno_datum, scale, trans_param);
    int Resized_ori_Height = int(scale * img_height);
    int Resized_ori_Width = int(scale * img_width);
    NormalizedBBox target_bbox = object_bboxes[object_bbox_index];
    float resized_xmin = target_bbox.xmin() * Resized_ori_Width;
    float resized_xmax = target_bbox.xmax() * Resized_ori_Width;
    float resized_ymin = target_bbox.ymin() * Resized_ori_Height;
    float resized_ymax = target_bbox.ymax() * Resized_ori_Height;
    float vibration_length = float(anchorStride[scaled_idx]);
    float offset_x = 0.f, offset_y = 0.f;
    caffe_rng_uniform(1, -vibration_length, vibration_length, &offset_x);
    caffe_rng_uniform(1, -vibration_length, vibration_length, &offset_y);
    float width_offset_ = (resized_xmin + resized_xmax) / 2 + offset_x - resized_width / 2;
    float height_offset_ = (resized_ymin + resized_ymax) / 2 + offset_y - resized_height / 2;
    float width_end_ = (resized_xmin + resized_xmax) / 2 + offset_x + resized_width / 2;
    float height_end_ = (resized_ymin + resized_ymax) / 2 + offset_y + resized_height / 2;
    float w_off = (float) width_offset_ / Resized_ori_Width;
    float h_off = (float) height_offset_ / Resized_ori_Height;
    float w_end = (float) width_end_ / Resized_ori_Width;
    float h_end = (float) height_end_ / Resized_ori_Height;
    sampled_bbox->set_xmin(w_off);
    sampled_bbox->set_ymin(h_off);
    sampled_bbox->set_xmax(w_end);
    sampled_bbox->set_ymax(h_end);

    SampleConstraint min_object_coverage_Constraint;
    min_object_coverage_Constraint.set_min_object_coverage(0.85);
    if(!SatisfySampleConstraint(*sampled_bbox, object_bboxes, min_object_coverage_Constraint)){
      resized_anno_datum->CopyFrom(anno_datum);
      sampled_bbox->set_xmin(0.f);
      sampled_bbox->set_ymin(0.f);
      sampled_bbox->set_xmax(1.f);
      sampled_bbox->set_ymax(1.f);
    }
    #if 0
    LOG(INFO)<<"scale: "<<scale << ", Resized_ori_Height: "<<Resized_ori_Height
              <<", Resized_ori_Width: "<<Resized_ori_Width<<", resized_xmin: "
              <<resized_xmin<<", resized_xmax: "<<resized_xmax<<", resized_ymin: "
              <<resized_ymin<<", resized_ymax: "<<resized_ymax<<", w_off: "<<width_offset_
              <<", h_off: "<<height_offset_<<", w_end: "<<width_end_<<", h_end: "<<height_end_
              <<", vibration_length: "<<vibration_length<<", offset_x: "<<offset_x<<", offset_y: "
              <<offset_y;
    #endif
  }else{
    resized_anno_datum->CopyFrom(anno_datum);
    sampled_bbox->set_xmin(0.f);
    sampled_bbox->set_ymin(0.f);
    sampled_bbox->set_xmax(1.f);
    sampled_bbox->set_ymax(1.f);
  }
}

void ResizedCropSample(const AnnotatedDatum& anno_datum, AnnotatedDatum* resized_anno_datum, 
                      float scale, const TransformationParameter& trans_param){
  const Datum datum = anno_datum.datum();
  const int img_width = datum.width();
  const int img_height = datum.height();
  int Resized_img_Height = int(img_height * scale);
  int Resized_img_Width = int(img_width * scale);
  // image data
  if (datum.encoded()) {
#ifdef USE_OPENCV
		CHECK(!(trans_param.force_color() && trans_param.force_gray()))
				<< "cannot set both force_color and force_gray";
		cv::Mat cv_img;
		if (trans_param.force_color() || trans_param.force_gray()) {
			// If force_color then decode in color otherwise decode in gray.
			cv_img = DecodeDatumToCVMat(datum, trans_param.force_color());
		} else {
			cv_img = DecodeDatumToCVMatNative(datum);
		}
		// Expand the image.
		cv::Mat resized_img;
    cv::resize(cv_img, resized_img, cv::Size(Resized_img_Width, Resized_img_Height), 0, 0,
                 cv::INTER_CUBIC);
		EncodeCVMatToDatum(resized_img, "jpg", resized_anno_datum->mutable_datum());
		resized_anno_datum->mutable_datum()->set_label(datum.label());
#else
		LOG(FATAL) << "Encoded datum requires OpenCV; compile with USE_OPENCV.";
#endif
	} else {
		if (trans_param.force_color() || trans_param.force_gray()) {
			LOG(ERROR) << "force_color and force_gray only for encoded datum";
		}
	}
  resized_anno_datum->set_type(anno_datum.type());
  RepeatedPtrField<AnnotationGroup>* Resized_anno_group = resized_anno_datum->mutable_annotation_group();
  // labels trans
  if (anno_datum.type() == AnnotatedDatum_AnnotationType_BBOX) {
		// Go through each AnnotationGroup.
    for (int g = 0; g < anno_datum.annotation_group_size(); ++g) {
      const AnnotationGroup& anno_group = anno_datum.annotation_group(g);
			AnnotationGroup transformed_anno_group ;

			for (int a = 0; a < anno_group.annotation_size(); ++a) {
				const Annotation& anno = anno_group.annotation(a);
				const NormalizedBBox& bbox = anno.bbox();
				// Adjust bounding box annotation.
				NormalizedBBox resize_bbox = bbox;
        float x_min = bbox.xmin() * img_width;
        float y_min = bbox.ymin() * img_height;
        float x_max = bbox.xmax() * img_width;
        float y_max = bbox.ymax() * img_height;
        x_min = std::max(0.f, x_min * Resized_img_Width / img_width);
        x_max = std::min(float(Resized_img_Width), x_max * Resized_img_Width / img_width);
        y_min = std::max(0.f, y_min * Resized_img_Height / img_height);
        y_max = std::min(float(Resized_img_Height), y_max * Resized_img_Height / img_height);
				resize_bbox.set_xmin(x_min / Resized_img_Width);
        resize_bbox.set_xmax(x_max / Resized_img_Width);
        resize_bbox.set_ymin(y_min / Resized_img_Height);
        resize_bbox.set_ymax(y_max / Resized_img_Height);
        Annotation* transformed_anno = transformed_anno_group.add_annotation();
        NormalizedBBox* transformed_bbox = transformed_anno->mutable_bbox();
				transformed_bbox->CopyFrom(resize_bbox);
			}
      transformed_anno_group.set_group_label(anno_group.group_label());
			Resized_anno_group->Add()->CopyFrom(transformed_anno_group);
    }
	} else {
		LOG(FATAL) << "Unknown annotation type.";
	}
  CHECK_GT(resized_anno_datum->datum().channels(), 0);
}

}  // namespace caffe
