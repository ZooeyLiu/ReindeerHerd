// NS_HerdCPU, Particle Update, Scratch Pad module "ReadAgent" (after the three Array "Get" nodes).
// Pure math, no data interface calls, so it runs on the CPU VM.
// Written branch-free: nested ternaries did not compile reliably for the CPU VM.
//
// Input pins:   State : int        (from AgentStates.Get Value)
// Output pins:  OutColor  : Linear Color  -> Map Set Particles.Color
//               OutPenned : bool          -> Map Set Particles.Penned

// 0 graze = brown, 1 spooked = red, 2 follow = yellow, 3 penned (killed anyway)
float S1 = State == 1 ? 1.0 : 0.0;
float S2 = State == 2 ? 1.0 : 0.0;
float S0 = 1.0 - S1 - S2;
OutColor = float4(0.45, 0.32, 0.2, 1) * S0 + float4(1, 0.15, 0.1, 1) * S1 + float4(1, 0.85, 0.2, 1) * S2;
OutPenned = State >= 3;
