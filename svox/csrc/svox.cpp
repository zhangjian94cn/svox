/*
 * Copyright Alex Yu 2021
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <torch/extension.h>
#include <c10/cuda/CUDAGuard.h>
#include <cstdint>
#include <vector>

namespace py = pybind11;

#define CHECK_CUDA(x) \
    TORCH_CHECK(x.type().is_cuda(), #x " must be a CUDA tensor")
#define CHECK_CONTIGUOUS(x) \
    TORCH_CHECK(x.is_contiguous(), #x " must be contiguous")
#define CHECK_INPUT(x) \
    CHECK_CUDA(x);     \
    CHECK_CONTIGUOUS(x)

torch::Tensor _query_vertical_cuda(torch::Tensor data, torch::Tensor child,
                                   torch::Tensor indices, torch::Tensor offset,
                                   torch::Tensor invradius);
torch::Tensor _query_vertical_backward_cuda(torch::Tensor child,
                                            torch::Tensor indices,
                                            torch::Tensor grad_output,
                                            torch::Tensor offset,
                                            torch::Tensor invradius);
void _assign_vertical_cuda(torch::Tensor data, torch::Tensor child,
                           torch::Tensor indices, torch::Tensor values,
                           torch::Tensor offset, torch::Tensor invradius);

torch::Tensor _volume_render_cuda(torch::Tensor data, torch::Tensor child,
                                  torch::Tensor origins, torch::Tensor dirs,
                                  torch::Tensor vdirs, torch::Tensor offset,
                                  torch::Tensor invradius, float step_size,
                                  float stop_thresh,
                                  float background_brightness);

/**
 * @param data (M, N, N, N, K)
 * @param child (M, N, N, N)
 * @param indices (Q, 3)
 * @return (Q, K)
 * */
torch::Tensor query_vertical(torch::Tensor data, torch::Tensor child,
                             torch::Tensor indices, torch::Tensor offset,
                             torch::Tensor invradius) {
    CHECK_INPUT(data);
    CHECK_INPUT(child);
    CHECK_INPUT(indices);
    CHECK_INPUT(offset);
    CHECK_INPUT(invradius);
    TORCH_CHECK(indices.dim() == 2);
    TORCH_CHECK(indices.is_floating_point());

    const at::cuda::OptionalCUDAGuard device_guard(device_of(data));
    return _query_vertical_cuda(data, child, indices, offset, invradius);
}

/**
 * @param data (M, N, N, N, K)
 * @param child (M, N, N, N)
 * @param indices (Q, 3)
 * @param grad_output (Q, K)
 * @return (M, N, N, N, K)
 * */
torch::Tensor query_vertical_backward(torch::Tensor child,
                                      torch::Tensor indices,
                                      torch::Tensor grad_output,
                                      torch::Tensor offset,
                                      torch::Tensor invradius) {
    CHECK_INPUT(child);
    CHECK_INPUT(grad_output);
    CHECK_INPUT(indices);
    CHECK_INPUT(offset);
    CHECK_INPUT(invradius);
    TORCH_CHECK(indices.dim() == 2);
    TORCH_CHECK(indices.is_floating_point());

    const at::cuda::OptionalCUDAGuard device_guard(device_of(grad_output));
    return _query_vertical_backward_cuda(child, indices, grad_output, offset,
                                         invradius);
}

/**
 * @param data (M, N, N, N, K)
 * @param child (M, N, N, N)
 * @param indices (Q, 3)
 * @param values (Q, K)
 * */
void assign_vertical(torch::Tensor data, torch::Tensor child,
                     torch::Tensor indices, torch::Tensor values,
                     torch::Tensor offset, torch::Tensor invradius) {
    CHECK_INPUT(data);
    CHECK_INPUT(child);
    CHECK_INPUT(indices);
    CHECK_INPUT(values);
    CHECK_INPUT(offset);
    CHECK_INPUT(invradius);
    TORCH_CHECK(indices.dim() == 2);
    TORCH_CHECK(values.dim() == 2);
    TORCH_CHECK(indices.is_floating_point());
    TORCH_CHECK(values.is_floating_point());

    const at::cuda::OptionalCUDAGuard device_guard(device_of(data));
    _assign_vertical_cuda(data, child, indices, values, offset, invradius);
}

torch::Tensor volume_render(torch::Tensor data, torch::Tensor child,
                            torch::Tensor origins, torch::Tensor dirs,
                            torch::Tensor vdirs, torch::Tensor offset,
                            torch::Tensor invradius, float step_size,
                            float stop_thresh, float background_brightness) {
    CHECK_INPUT(data);
    CHECK_INPUT(child);
    CHECK_INPUT(origins);
    CHECK_INPUT(dirs);
    CHECK_INPUT(vdirs);
    CHECK_INPUT(offset);
    CHECK_INPUT(invradius);
    TORCH_CHECK(data.size(-1) >= 4);
    return _volume_render_cuda(data, child, origins, dirs, vdirs, offset,
                               invradius, step_size, stop_thresh,
                               background_brightness);
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
    m.def("query_vertical", &query_vertical, "Query tree at coords [0, 1)",
          py::arg("data"), py::arg("child"), py::arg("indices"),
          py::arg("offset"), py::arg("invradius"));
    m.def("query_vertical_backward", &query_vertical_backward,
          "Backwards pass for query_vertical", py::arg("child"),
          py::arg("indices"), py::arg("grad_output"), py::arg("offset"),
          py::arg("invradius"));
    m.def("assign_vertical", &assign_vertical,
          "Assign tree at given coords [0, 1)", py::arg("data"),
          py::arg("child"), py::arg("indices"), py::arg("values"),
          py::arg("offset"), py::arg("invradius"));
    m.def("volume_render", &volume_render, py::arg("data"), py::arg("child"),
          py::arg("origins"), py::arg("dirs"), py::arg("vdirs"),
          py::arg("offset"), py::arg("invradius"), py::arg("step_size"),
          py::arg("stop_thresh"), py::arg("background_brightness"));
}
