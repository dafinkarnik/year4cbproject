/*
*  WEBRTC-CB
*/

#ifndef MODULES_CONGESTION_CONTROLLER_CIRCUIT_BREAKER_H_
#define MODULES_CONGESTION_CONTROLLER_CIRCUIT_BREAKER_H_

#include <initializer_list>

#include "common_types.h"  // NOLINT(build/include)
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_sender.h"
#include <vector>
#include <ctime>
#include <sstream>

namespace webrtc {

	enum CbTriggerTypes {
		kNone = 0,
		kRtcpTimeout = 1,
		kMediaTimeout = 2,
		kCongestion = 3,
		kMediaUsability = 4
	};

	struct ReportBlockInfo {
		uint32_t dlsr;
		uint8_t fraction_lost;
	};

	struct MediaTimeoutStruct {
		uint32_t media_timeout;
		uint32_t media_timeout_counter;
		uint32_t extended_highest_sequence_number;
	};

	class CircuitBreaker {
	public:
		CircuitBreaker(RTPSender* rtp_sender, Clock* clock);
		~CircuitBreaker();

		void OnReceivedRtcpReportBlocks(const ReportBlockList& report_blocks);

		void RecalculateCbInterval(uint32_t sender_ssrc);

		uint32_t CalculateMediaTimeout(uint32_t sender_ssrc);

		void PushPacketSize(size_t packet_size);

		double GetInterFrameArrival();

		void SetRtcpInterval(uint32_t rtcp_interval, uint32_t rtcp_interval_at_receiver_estimate);

		void SetVideoCodecType(RtpVideoCodecTypes video_type);

		void UpdateSendingStatus(bool sending);

		std::array<bool, 4> GetCbStatus();

		void ClearCb();

	private:
        static const int RTCP_VIDEO_INTERVAL_MS = 1000;
        static const int NON_REPORTING_THRESHOLD = 5; 
        
		bool media_timeout_cb_ = false;
		bool rtcp_timeout_cb_ = false;
		bool congestion_cb_ = false;
		bool media_usability_cb_ = false;
		void HandleTriggeredCircuitBreaker(CbTriggerTypes flag);
        

		RTPSender* rtp_sender_;
		Clock* clock_;
		std::string codec_;
		bool sending_ = true;
		using ReportBlockInfoList = std::list<ReportBlockInfo>;
		using CbIntervalRttSmoothedPair = std::pair <uint32_t, uint64_t>;
		using CbIntrvlRttReportBlockListPair = std::pair<CbIntervalRttSmoothedPair, ReportBlockInfoList>;
		using ReportBlockInfoMap = std::map<uint32_t, CbIntrvlRttReportBlockListPair>;
		using MediaTimeoutMap = std::map<uint32_t, MediaTimeoutStruct>;
		ReportBlockInfoMap* previous_report_blocks_;
        MediaTimeoutMap* media_timeout_map_;
		uint32_t initial_cb_interval_;
		uint32_t frame_group_size_ = 1;
		double media_framing_interval_;
		uint32_t rtcp_interval_;
		uint32_t rtcp_interval_sender_estimate_;
		std::vector<uint32_t> packet_sizes_;
		uint32_t packet_size_;
	};

}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_CIRCUIT_BREAKER_H_