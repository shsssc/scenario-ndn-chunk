/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016-2020, Regents of the University of California,
 *                          Colorado State University,
 *                          University Pierre & Marie Curie, Sorbonne University.
 *
 * This file is part of ndn-tools (Named Data Networking Essential Tools).
 * See AUTHORS.md for complete list of ndn-tools authors and contributors.
 *
 * ndn-tools is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndn-tools is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndn-tools, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 *
 * @author Wentao Shang
 * @author Steve DiBenedetto
 * @author Andrea Tosatto
 */

#ifndef NDN_TOOLS_CHUNKS_CATCHUNKS_CONSUMER_APP
#define NDN_TOOLS_CHUNKS_CATCHUNKS_CONSUMER_APP

#include "consumer.hpp"
#include "discover-version.hpp"
#include "pipeline-interests-aimd.hpp"
#include "pipeline-interests-cubic.hpp"
#include "boost/iostreams/stream.hpp"
#include "boost/iostreams/device/null.hpp"
//#include "pipeline-interests-fixed.hpp"
//#include "statistics-collector.hpp"

namespace ndn {
    namespace chunks {

        class consumerApp {
        public:
            explicit consumerApp(const std::string& uri) :nullOstream(( boost::iostreams::null_sink() ) ) {
                this->uri = uri;
                discover = make_unique<DiscoverVersion>(m_face, Name(uri), options);
                optionsRttEst = make_shared<RttEstimatorWithStats::Options>();
                optionsRttEst->alpha = rtoAlpha;
                optionsRttEst->beta = rtoBeta;
                optionsRttEst->k = rtoK;
                optionsRttEst->initialRto = 1_s;
                optionsRttEst->minRto = time::milliseconds(minRto);
                optionsRttEst->maxRto = time::milliseconds(maxRto);
                optionsRttEst->rtoBackoffMultiplier = 2;

                rttEstimator = make_unique<RttEstimatorWithStats>(std::move(optionsRttEst));
                if (pipelineType == "aimd") {
                    adaptivePipeline = make_unique<PipelineInterestsAimd>(m_face, *rttEstimator, options);
                } else {
                    adaptivePipeline = make_unique<PipelineInterestsCubic>(m_face, *rttEstimator, options);
                }
                consumer = make_unique<Consumer>(nullOstream);
            }

            void run() {
                consumer->run(std::move(discover), std::move(adaptivePipeline));
            }

        private:
            shared_ptr<RttEstimatorWithStats::Options> optionsRttEst;
            unique_ptr<RttEstimatorWithStats> rttEstimator;
            std::unique_ptr<Consumer> consumer;
            Face m_face;
            unique_ptr<PipelineInterestsAdaptive> adaptivePipeline;
            unique_ptr<DiscoverVersion> discover;
            std::string uri;
            std::string pipelineType = "aimd";
            std::string cwndPath;
            std::string rttPath;
            time::milliseconds::rep minRto = 200;
            time::milliseconds::rep maxRto = 60000;
            double rtoAlpha = 0.125;
            double rtoBeta = 0.25;
            int rtoK = 8;
            Options options;
            boost::iostreams::stream< boost::iostreams::null_sink > nullOstream;
        };

    } // namespace chunks
} // namespace ndn

#endif
