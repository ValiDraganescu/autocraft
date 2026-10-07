VIDEO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
export HYPERFRAMES_FFMPEG_PATH="$VIDEO_ROOT/.tools/ffmpeg-bin/bin/ffmpeg"
export HYPERFRAMES_FFPROBE_PATH="$VIDEO_ROOT/.tools/ffmpeg-bin/bin/ffprobe"
export PATH="$VIDEO_ROOT/node_modules/.bin:$VIDEO_ROOT/.tools/ffmpeg-bin/bin:$PATH"
export HYPERFRAMES_NO_TELEMETRY=1
export DO_NOT_TRACK=1
export HYPERFRAMES_SKIP_SKILLS=1
