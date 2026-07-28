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

		// Roblox's older names for three of the signals above. They are kept
		// because a lot of existing Luau still connects to them.
		G_SIGNAL(Heartbeat, double);
		G_SIGNAL(RenderStepped, double);
		// Stepped is the one legacy signal with a different shape:
		// (time the engine has been running, delta time)
		typedef std::tuple<double, double> SteppedArguments;
		G_SIGNAL(Stepped, SteppedArguments);

		// Fired by the engine in frame order; each one also fires the legacy
		// alias so scripts do not have to care which name they used
		void FireSimulation(double deltaTime);
		void FirePostSimulation(double deltaTime);
		void FireRender(double deltaTime);

		double GetElapsedTime() const;

		// NOTE: Gargantuan has no client/server split yet, so these report the
		// single local session rather than a real network role
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
