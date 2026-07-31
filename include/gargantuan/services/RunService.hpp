#pragma once

#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Signal.hpp"

#include <tuple>

namespace gargantuan {
	class RunService : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		G_SIGNAL(PreSimulation, double);
		G_SIGNAL(PostSimulation, double);
		G_SIGNAL(PreAnimation, double);
		G_SIGNAL(PreRender, double);

		G_SIGNAL(Heartbeat, double);
		G_SIGNAL(RenderStepped, double);
		typedef std::tuple<double, double> SteppedArguments;
		G_SIGNAL(Stepped, SteppedArguments);

		void FireSimulation(double deltaTime);
		void FirePostSimulation(double deltaTime);
		void FireRender(double deltaTime);

		double GetElapsedTime() const;

		bool IsClient() const;
		bool IsServer() const;
		bool IsStudio() const;
		bool IsEdit() const;
		bool IsRunning() const;
		bool IsRunMode() const;

	  private:
		double Elapsed = 0.0;
	};
}
