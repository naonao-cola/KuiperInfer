//
// Created by fss on 22-11-13.
//

#include "softmax.hpp"
#include <glog/logging.h>
#include "data/tensor_util.hpp"
#include "layer/abstract/layer_factory.hpp"
namespace kuiper_infer {
SoftmaxLayer::SoftmaxLayer(int dim) : Layer("Softmax"), softmax_dim_(dim) {}

InferStatus SoftmaxLayer::Forward(
    const std::vector<std::shared_ptr<Tensor<float>>>& inputs,
    std::vector<std::shared_ptr<Tensor<float>>>& outputs) {
  if (inputs.empty()) {
    LOG(ERROR) << "The input feature map of softmax layer is empty";
    return InferStatus::kInferFailedInputEmpty;
  }

  if (inputs.size() != outputs.size()) {
    LOG(ERROR) << "The input and output size is not adapting";
    return InferStatus::kInferFailedInputOutSizeAdaptingError;
  }

  const uint32_t batch_size = inputs.size();
#pragma omp parallel for num_threads(batch_size)
  for (uint32_t i = 0; i < batch_size; ++i) {
    const std::shared_ptr<Tensor<float>>& input = inputs.at(i);
    CHECK(input != nullptr && !input->empty())
        << "The input feature map for softmax layer is empty";

    std::shared_ptr<Tensor<float>> output = outputs.at(i);
    if (output == nullptr || output->empty()) {
      output = std::make_shared<Tensor<float>>(input->shapes());
      outputs.at(i) = output;
    }
    CHECK(input->shapes() == output->shapes());
    int dim = this->softmax_dim_;
    std::vector<uint32_t> raw_shapes = input->raw_shapes();

    if (dim < 0) {
      dim += int(raw_shapes.size());
    }
    CHECK(dim >= 0 && dim <= 2);
    CHECK_LT(dim, raw_shapes.size());

    if (dim < 0 || dim >= 3) {
      LOG(FATAL) << "Error softmax dimension, which need between 0 and 2, "
                    "but dimension is "
                 << dim;
    }
    const auto& output_origin_shapes = output->shapes();

    const uint32_t padding_size_num = 3 - raw_shapes.size();
    for (uint32_t j = 0; j < padding_size_num; ++j) {
      raw_shapes.push_back(1);
    }

    const uint32_t inner_sizes = std::accumulate(
        raw_shapes.begin() + dim + 1, raw_shapes.end(), 1, std::multiplies());
    const uint32_t outer_sizes = std::accumulate(
        raw_shapes.begin(), raw_shapes.begin() + dim, 1, std::multiplies());
    const uint32_t axis_sizes = raw_shapes.at(dim);
    CHECK_EQ(axis_sizes * outer_sizes * inner_sizes, input->size());

    auto input_ = TensorClone(input);
    input_->Reshape({outer_sizes, axis_sizes, inner_sizes}, true);
    output->Reshape({outer_sizes, axis_sizes, inner_sizes}, true);
    for (uint32_t outer_size = 0; outer_size < outer_sizes; ++outer_size) {
      for (uint32_t inner_size = 0; inner_size < inner_sizes; ++inner_size) {
        float max_value = std::numeric_limits<float>::lowest();
        float sum_value = 0.f;
        for (uint32_t axis_size = 0; axis_size < axis_sizes; ++axis_size) {
          float cur_value = input_->at(outer_size, axis_size, inner_size);
          if (cur_value > max_value) {
            max_value = cur_value;
          }
        }

        for (uint32_t axis_size = 0; axis_size < axis_sizes; ++axis_size) {
          float cur_value = input_->at(outer_size, axis_size, inner_size);
          sum_value += std::exp(cur_value - max_value);
        }

        for (uint32_t axis_size = 0; axis_size < axis_sizes; ++axis_size) {
          float cur_value = input_->at(outer_size, axis_size, inner_size);
          output->at(outer_size, axis_size, inner_size) =
              std::exp(cur_value - max_value) / sum_value;
        }
      }
    }
    output->Reshape(output_origin_shapes, true);
  }
  return InferStatus::kInferSuccess;
}
ParseParameterAttrStatus SoftmaxLayer::GetInstance(
    const std::shared_ptr<RuntimeOperator>& op,
    std::shared_ptr<Layer>& softmax_layer) {
  CHECK(op != nullptr) << "SoftMax operator is nullptr";
  const auto& params = op->params;
  if (params.find("dim") == params.end()) {
    return ParseParameterAttrStatus::kParameterMissingDim;
  }
  auto dim_param = params.at("dim");
  auto dim = dynamic_cast<RuntimeParameterInt*>(dim_param);
  if (dim == nullptr) {
    return ParseParameterAttrStatus::kParameterMissingDim;
  }
  softmax_layer = std::make_shared<SoftmaxLayer>(dim->value);  // 创建softmax层
  return ParseParameterAttrStatus::kParameterAttrParseSuccess;
}
LayerRegistererWrapper kSoftMaxGetInstanceNN("nn.Softmax",
                                             SoftmaxLayer::GetInstance);
LayerRegistererWrapper kSoftMaxGetInstanceF("F.softmax",
                                            SoftmaxLayer::GetInstance);
}  // namespace kuiper_infer