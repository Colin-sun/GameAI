(function (root) {
  "use strict";

  const MAGIC = "GAI1";
  const HEADER_VERSION = 1;
  const BOARD_SIZE = 9;
  const CELLS = BOARD_SIZE * BOARD_SIZE;

  function align4(value) {
    return (value + 3) & ~3;
  }

  function relu(values) {
    const result = new Float32Array(values);
    for (let index = 0; index < result.length; index += 1) {
      if (result[index] < 0) result[index] = 0;
    }
    return result;
  }

  function conv2d(input, weight, bias, outputChannels, inputChannels, kernelSize) {
    const output = new Float32Array(outputChannels * CELLS);
    const padding = Math.floor(kernelSize / 2);
    const kernelArea = kernelSize * kernelSize;
    for (let outputChannel = 0; outputChannel < outputChannels; outputChannel += 1) {
      const outputOffset = outputChannel * CELLS;
      const weightChannelOffset = outputChannel * inputChannels * kernelArea;
      for (let row = 0; row < BOARD_SIZE; row += 1) {
        for (let col = 0; col < BOARD_SIZE; col += 1) {
          let sum = bias[outputChannel];
          for (let inputChannel = 0; inputChannel < inputChannels; inputChannel += 1) {
            const inputOffset = inputChannel * CELLS;
            const weightOffset = weightChannelOffset + inputChannel * kernelArea;
            for (let kernelRow = 0; kernelRow < kernelSize; kernelRow += 1) {
              const sourceRow = row + kernelRow - padding;
              if (sourceRow < 0 || sourceRow >= BOARD_SIZE) continue;
              for (let kernelCol = 0; kernelCol < kernelSize; kernelCol += 1) {
                const sourceCol = col + kernelCol - padding;
                if (sourceCol < 0 || sourceCol >= BOARD_SIZE) continue;
                sum += input[inputOffset + sourceRow * BOARD_SIZE + sourceCol] *
                  weight[weightOffset + kernelRow * kernelSize + kernelCol];
              }
            }
          }
          output[outputOffset + row * BOARD_SIZE + col] = sum;
        }
      }
    }
    return output;
  }

  function dense(input, weight, bias, outputSize) {
    const inputSize = input.length;
    const output = new Float32Array(outputSize);
    for (let outputIndex = 0; outputIndex < outputSize; outputIndex += 1) {
      let sum = bias[outputIndex];
      const weightOffset = outputIndex * inputSize;
      for (let inputIndex = 0; inputIndex < inputSize; inputIndex += 1) {
        sum += input[inputIndex] * weight[weightOffset + inputIndex];
      }
      output[outputIndex] = sum;
    }
    return output;
  }

  function softmax(logits) {
    let max = -Infinity;
    for (const value of logits) max = Math.max(max, value);
    const output = new Float32Array(logits.length);
    let total = 0;
    for (let index = 0; index < logits.length; index += 1) {
      output[index] = Math.exp(logits[index] - max);
      total += output[index];
    }
    if (!(total > 0) || !Number.isFinite(total)) {
      output.fill(1 / logits.length);
      return output;
    }
    for (let index = 0; index < output.length; index += 1) output[index] /= total;
    return output;
  }

  class PriorModel {
    constructor(metadata, weights) {
      this.metadata = metadata;
      this.weights = weights;
      this.channels = metadata.channels;
      this.blocks = metadata.blocks;
      if (metadata.ruleVersion !== "majority-utt-v1" ||
          metadata.inputPlanes !== 10 || metadata.actionSize !== 81) {
        throw new Error("Prior model metadata does not match majority-utt-v1.");
      }
    }

    static fromArrayBuffer(buffer) {
      const bytes = new Uint8Array(buffer);
      const view = new DataView(buffer);
      let offset = 0;
      const readU32 = () => {
        if (offset + 4 > bytes.length) throw new Error("Truncated prior model header.");
        const value = view.getUint32(offset, true);
        offset += 4;
        return value;
      };
      const readString = () => {
        const length = readU32();
        if (offset + length > bytes.length) throw new Error("Truncated prior model name.");
        const value = new TextDecoder().decode(bytes.subarray(offset, offset + length));
        offset += length;
        return value;
      };
      const magic = new TextDecoder().decode(bytes.subarray(0, 4));
      offset = 4;
      if (magic !== MAGIC || readU32() !== HEADER_VERSION) {
        throw new Error("Unsupported prior model format.");
      }
      const metadata = {
        inputPlanes: readU32(),
        actionSize: readU32(),
        channels: readU32(),
        blocks: readU32(),
        ruleVersion: readString(),
        checkpoint: readString(),
      };
      const tensorCount = readU32();
      const weights = {};
      for (let index = 0; index < tensorCount; index += 1) {
        const name = readString();
        const length = readU32();
        offset = align4(offset);
        const byteLength = length * 4;
        if (offset + byteLength > bytes.length) throw new Error(`Truncated tensor ${name}.`);
        weights[name] = new Float32Array(buffer, offset, length);
        offset += byteLength;
      }
      return new PriorModel(metadata, weights);
    }

    static async load(url) {
      const response = await fetch(url);
      if (!response.ok) throw new Error(`Prior model request failed: ${response.status}`);
      return PriorModel.fromArrayBuffer(await response.arrayBuffer());
    }

    predict(encodedState) {
      if (!encodedState || encodedState.length !== 810) {
        throw new Error("Prior model expects a 10-plane 9x9 state.");
      }
      const w = this.weights;
      let features = relu(conv2d(
        encodedState,
        w["stem.weight"],
        w["stem.bias"],
        this.channels,
        10,
        3,
      ));
      for (let block = 0; block < this.blocks; block += 1) {
        const residual = features;
        features = relu(conv2d(
          features,
          w[`residual.${block}.conv1.weight`],
          w[`residual.${block}.conv1.bias`],
          this.channels,
          this.channels,
          3,
        ));
        features = conv2d(
          features,
          w[`residual.${block}.conv2.weight`],
          w[`residual.${block}.conv2.bias`],
          this.channels,
          this.channels,
          3,
        );
        for (let index = 0; index < features.length; index += 1) {
          features[index] = Math.max(0, features[index] + residual[index]);
        }
      }

      const policyFeatures = relu(conv2d(
        features,
        w["policy_conv.weight"],
        w["policy_conv.bias"],
        32,
        this.channels,
        1,
      ));
      const policy = softmax(dense(
        policyFeatures,
        w["policy_fc.weight"],
        w["policy_fc.bias"],
        81,
      ));

      const valueFeatures = relu(conv2d(
        features,
        w["value_conv.weight"],
        w["value_conv.bias"],
        32,
        this.channels,
        1,
      ));
      const valueHidden = relu(dense(
        valueFeatures,
        w["value_fc1.weight"],
        w["value_fc1.bias"],
        128,
      ));
      const valueRaw = dense(valueHidden, w["value_fc2.weight"], w["value_fc2.bias"], 1)[0];
      return { policy, value: Math.tanh(valueRaw) };
    }
  }

  const api = { PriorModel, conv2d, dense, softmax };
  root.GameAIPriorModel = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis === "undefined" ? this : globalThis);
