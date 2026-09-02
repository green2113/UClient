#ifndef ENGINE_SHARED_VIDEO_H
#define ENGINE_SHARED_VIDEO_H

#include <base/time.h>

#include <cstdint>
#include <functional>

typedef std::function<void(short *pFinalOut, unsigned Frames)> ISoundMixFunc;
typedef std::function<void()> FVideoStopPumpCallback;

enum class EVideoFormat
{
	Mp4 = 0,
	Gif = 1,
};

class IVideo
{
public:
	virtual ~IVideo() = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual void Pause(bool Pause) = 0;
	virtual bool IsRecording() = 0;

	virtual void NextVideoFrame() = 0;
	virtual void NextVideoFrameThread() = 0;

	virtual void NextAudioFrame(ISoundMixFunc Mix) = 0;
	virtual void NextAudioFrameTimeline(ISoundMixFunc Mix) = 0;

	static IVideo *Current() { return ms_pCurrentVideo; }

	static int64_t Time() { return ms_Time; }
	static float LocalTime() { return ms_LocalTime; }
	static void SetLocalStartTime(int64_t LocalStartTime) { ms_LocalStartTime = LocalStartTime; }
	static void SetFPS(int FPS) { ms_TickTime = time_freq() / FPS; }
	static void SetStopPumpCallback(FVideoStopPumpCallback Callback) { ms_StopPumpCallback = std::move(Callback); }
	static void InvokeStopPumpCallback()
	{
		if(ms_StopPumpCallback)
			ms_StopPumpCallback();
	}

protected:
	static IVideo *ms_pCurrentVideo;
	static FVideoStopPumpCallback ms_StopPumpCallback;
	static int64_t ms_Time;
	static int64_t ms_LocalStartTime;
	static float ms_LocalTime;
	static int64_t ms_TickTime;
};

#endif
