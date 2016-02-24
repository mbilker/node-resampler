const binding = require('./build/Debug/resampler');
const stream = require('stream');
const util = require('util');

const debug = require('debug')('resampler');

function Resampler(inputRate, outputRate, quality) {
  if (quality == null) {
    quality = Resampler.QUALITY_HI;
  }

  stream.Transform.call(this);

  debug('new', {inputRate, outputRate, quality});

  this.resampler = new binding.Resampler(inputRate, outputRate, quality);
}
util.inherits(Resampler, stream.Transform);

Resampler.prototype._resample = function _resample(samples, callback) {
  debug('_resample', {samples, samplesLength: samples.length});

  this.resampler.resample(samples, (err, resampled) => {
    if (err != null) {
      throw err;
    }

    debug('_resample.sampled', {resampled, resampledLength: resampled.length});

    this.push(resampled);
    callback()
  });
};

Resampler.prototype._transform = function _transform(chunk, encoding, callback) {
  //debug('_transform', {chunk, encoding});

  if (this.resampler.opened) {
    return this._resample(chunk, callback);
  }

  this.resampler.open((err) => {
    if (err != null) {
      throw err;
    }

    this._resample(chunk, callback);
  });
};

Resampler.prototype._flush = function _flush(callback) {
  debug('_flush');

  if (!this.resampler.opened) {
    return callback();
  }

  this.resampler.flush((err, resampled) => {
    if (err != null) {
      throw err;
    }

    this.push(resampled);
    this.resampler.close((err) => {
      if (err != null) {
        throw err;
      }

      callback();
    });
  });
};

Resampler.QUALITY_HI = 1;
Resampler.QUALITY_LO = 0;

module.exports = Resampler;
