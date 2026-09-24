import Bunzip from 'seek-bzip';
import {Buffer} from 'buffer';

// seek-bzip's block decoder uses the Node Buffer API internally. This pinned
// browser implementation stays inside the dedicated network worker.
globalThis.Buffer ??= Buffer;

export const MAX_LXMF_BYTES = 16384;
export const compressionProvider = {
  // No outbound compression is needed for the short browser read requests.
  compress: raw => raw,
  decompress(raw, advertisedSize) {
    if (!(raw instanceof Uint8Array) || raw.length > MAX_LXMF_BYTES ||
        !Number.isInteger(advertisedSize) || advertisedSize < 1 || advertisedSize > MAX_LXMF_BYTES) {
      throw Error('Resource decompression limit');
    }
    const output = new Uint8Array(advertisedSize);
    let written = 0, read = 0;
    const input = new Bunzip.Stream(), sink = new Bunzip.Stream();
    input.readByte = () => { if (read >= raw.length) throw Error('Truncated bzip2'); return raw[read++]; };
    input.eof = () => read >= raw.length;
    sink.writeByte = value => {
      if (written >= output.length) throw Error('Resource expands beyond advertised size');
      output[written++] = value;
    };
    Bunzip.decode(input, sink);
    if (written !== advertisedSize) throw Error('Resource decompressed size mismatch');
    return output;
  },
};
