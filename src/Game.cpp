#include "Game.h"

#include <cstdio>

namespace zp
{

	// Flight model. Gravity is gentle and terminal velocity is low on purpose:
	// this is a puzzle now, and the player needs time to think.
	static const float SHIP_RADIUS = 1.05f; // matches the lathed hull, fins included
	static const float POD_RADIUS = 1.55f;
	static const float THRUST_ACC = 21.0f; // about 4:1 against gravity
	static const float RETRO_ACC = 12.0f;  // deliberately weaker, so turning still matters
	static const float ANG_RATE = 3.0f;
	static const float SOFT_SPEED = 23.0f; // drag ramps in above this
	static const float MAX_SPEED = 32.0f;
	static const float TETHER_LEN = 10.0f;
	static const int ROPE_LINKS = 3;
	static const float FUEL_MAX = 100.0f;
	static const float BULLET_SPEED = 52.0f;
	static const float FIXED_DT = 1.0f / 120.0f;
	// Camera throw at full shake, in world units. Twice this made a big blast
	// throw the view so far the player lost the ship.
	static const float SHAKE_DISTANCE = 1.6f;

	// The shield capacitor is the mistake budget. Fuel is the only thing the player
	// spends, and it feeds both the engines and this - so "how clumsy can I afford
	// to be" and "can I get out" are the same question.
	static const float SHIELD_MAX = 100.0f;
	static const float SHIELD_RECHARGE = 17.0f;  // points per second
	static const float SHIELD_FUEL_COST = 0.15f; // fuel burned per shield point
	static const float SHIELD_BUDGET = 260.0f;   // spending this much scores zero integrity
	static const float BULLET_SHIELD_COST = 4.0f;
	static const float IMPACT_FREE = 22.0f; // contact impulse absorbed for nothing
	static const float IMPACT_SCALE = 0.40f;
	static const float HARD_HIT = 25.0f; // shield cost that knocks the emitter offline
	static const float LOCKOUT = 2.5f;

	static Vec2 upFromAngle(float a)
	{
		return fromAngle(a + kPi * 0.5f);
	}

	// ---------------------------------------------------------------- lifecycle

	bool Game::Init(Synth* s, const Input* in)
	{
		audio_ = s;
		win_ = in;

		// Made things are smooth and take a highlight; natural things are faceted
		// in the rock's colours instead. Nothing gets both.
		pedestalFigure_ = BuildPedestal();
		fuelFigure_ = BuildFuelCell();
		turretBase_ = BuildTurretBase();
		turretBarrel_ = BuildTurretBarrel();
		factoryFigure_ = BuildFactory();
		factoryCore_ = BuildFactoryCore();
		mineFigure_ = BuildMine();
		droneFigure_ = BuildDrone();
		wellRing_ = BuildWellRing();
		wellCore_ = BuildWellCore();
		relicFigure_ = BuildRelic();
		tankFigure_ = BuildTank();
		crateFigure_ = BuildCrate();
		boulderFigure_ = BuildBoulder();
		slabFigure_ = BuildSlab();

		parts_.reserve(14000);
		shards_.reserve(2000);
		bullets_.reserve(256);

		profile_.Load();
		BuildThumbnails();

		StartPlanet(0);
		mode_ = Mode::Title;
		modeTime_ = 0;
		audio_->SetMusic(0, spec_.seed);
		return true;
	}

	// Every planet card shows the real cavern, generated once at a quarter of the
	// working resolution from the same seed the mission will use.
	void Game::BuildThumbnails()
	{
		specs_.clear();
		specs_.reserve(kPlanetCount);
		thumbs_.assign((size_t)kPlanetCount * kThumbW * kThumbH, 0);
		Field coarse;
		for (int i = 0; i < kPlanetCount; ++i)
		{
			LevelSpec s = LevelSpec::For(i);
			specs_.push_back(s);
			coarse.GenerateCoarse(s);
			coarse.RasterThumb(&thumbs_[(size_t)i * kThumbW * kThumbH], kThumbW, kThumbH);
		}
	}

	void Game::StartPlanet(int index)
	{
		spec_ = LevelSpec::For(index);
		gridSel_ = spec_.index;
		field_.Generate(spec_);
		BuildTerrain(field_, spec_, terrain_);

		world_.Reset(&field_, Vec2(0, -spec_.gravity));
		ropeLinks_.clear();
		ropeJoints_.clear();

		{
			Body b;
			b.pos = field_.startPos;
			b.shape = MakeCircle(SHIP_RADIUS);
			b.restitution = 0.32f;
			b.friction = 0.30f;
			b.linDamp = 0.05f;
			b.flags = BF_FIXED_ROT | BF_NO_SLEEP;
			// Density chosen to keep the mass near 6 after the radius change, so
			// every impact threshold below stays calibrated.
			shipBody_ = world_.AddBody(b, 1.75f);
		}
		{
			Body o;
			o.pos = field_.orbPos;
			o.shape = MakeCircle(POD_RADIUS);
			o.restitution = 0.24f;
			o.friction = 0.65f;
			o.linDamp = 0.10f;
			podBody_ = world_.AddBody(o, 0.55f);
		}

		// Props. The generator decided what and where; this decides how it is held
		// together and therefore how it comes apart.
		props_.clear();
		for (auto& ps : field_.props)
		{
			Prop p;
			p.kind = ps.kind;
			p.signature = ps.signature;
			p.halfW = ps.halfW;
			p.halfH = ps.halfH;
			p.radius = ps.radius;

			Body pb;
			pb.pos = ps.pos;
			pb.ang = ps.ang;
			switch (ps.kind)
			{
			case PROP_TANK:
				pb.shape = MakeCircle(ps.radius);
				pb.restitution = 0.10f;
				pb.friction = 0.55f;
				p.hp = 1.0f;
				break;
			case PROP_BOULDER:
				pb.shape = MakeCircle(ps.radius);
				pb.restitution = 0.04f;
				pb.friction = 0.85f;
				p.hp = 16.0f;
				break;
			case PROP_PLATFORM:
				pb.shape = MakeBox(ps.halfW, ps.halfH);
				pb.restitution = 0.05f;
				pb.friction = 0.75f;
				p.hp = 30.0f;
				break;
			default:
				pb.shape = MakeBox(ps.halfW, ps.halfH);
				pb.restitution = 0.08f;
				pb.friction = 0.70f;
				p.hp = 5.0f;
				break;
			}
			p.body = world_.AddBody(pb, ps.density);

			if (ps.anchor == ANCHOR_PIN)
			{
				p.anchorA = ps.anchorPoint;
				p.jointA = world_.AddPin(p.body, ps.anchorPoint, ps.breakImpulse);
			}
			else if (ps.anchor == ANCHOR_SHELF)
			{
				// Hinge at the wall, prop at the far end. The prop is the weak link,
				// so the shelf survives a knock but not a charge underneath it.
				p.anchorA = ps.anchorPoint;
				p.jointA = world_.AddPin(p.body, ps.anchorPoint, ps.breakImpulse * 2.5f);
				float outward = ps.anchorPoint.x < ps.pos.x ? 1.0f : -1.0f;
				Vec2 localB(ps.halfW * 0.85f * outward, 0);
				p.anchorB = ps.anchorPoint2;
				p.jointB = world_.AddDistance(p.body, -1, localB, ps.anchorPoint2,
				                              length(ps.pos + localB - ps.anchorPoint2), false, ps.breakImpulse);
			}
			else if (ps.anchor == ANCHOR_STRUTS)
			{
				// Two breakable legs. Shooting one is enough to tip a drum over.
				Vec2 la(-ps.halfW * 0.7f, -ps.halfH * 0.7f);
				Vec2 lb(ps.halfW * 0.7f, -ps.halfH * 0.7f);
				p.anchorA = ps.anchorPoint + Vec2(-ps.halfW, 0);
				p.anchorB = ps.anchorPoint + Vec2(ps.halfW, 0);
				p.jointA = world_.AddDistance(p.body, -1, la, p.anchorA, length(ps.pos + la - p.anchorA), false,
				                              ps.breakImpulse);
				p.jointB = world_.AddDistance(p.body, -1, lb, p.anchorB, length(ps.pos + lb - p.anchorB), false,
				                              ps.breakImpulse);
			}
			props_.push_back(p);
		}

		relics_.clear();
		for (auto& p : field_.relicPos) relics_.push_back({p, false, rng_.f01() * kTau});

		turrets_.clear();
		for (auto& t : field_.turrets)
		{
			Turret tu;
			tu.pos = t.pos;
			tu.normal = t.normal;
			tu.baseAngle = atan2f(t.normal.y, t.normal.x);
			tu.aim = tu.baseAngle;
			tu.cooldown = rng_.range(0.9f, 2.8f);
			turrets_.push_back(tu);
		}

		fuel_cells_.clear();
		for (auto& p : field_.fuelPos) fuel_cells_.push_back({p, true, rng_.f01() * kTau});

		mines_.clear();
		for (auto& p : field_.minePos)
		{
			Mine m;
			m.p = p;
			m.v = rng_.unitVec() * rng_.range(0.4f, 2.0f);
			m.phase = rng_.f01() * kTau;
			m.spin = rng_.range(-0.9f, 0.9f);
			mines_.push_back(m);
		}

		drones_.clear();
		for (auto& p : field_.dronePos)
		{
			Drone d;
			d.p = d.home = p;
			d.ang = rng_.f01() * kTau;
			d.wander = rng_.f01() * kTau;
			d.hp = 1.5f + 2.5f * spec_.droneStrength;
			drones_.push_back(d);
		}

		wells_.clear();
		for (auto& w : field_.wells)
		{
			Well ww;
			ww.pos = w.pos;
			ww.strength = w.strength;
			ww.radius = w.radius;
			ww.hp = w.strength > 0 ? 3.0f : 4.0f;
			wells_.push_back(ww);
		}
		wellBoost_ = 1.0f;

		// One new thing at a time, named on the briefing card. Anything the player
		// has already met stays silent.
		newThreat_.clear();
		if (!metMine_ && !mines_.empty())
		{
			newThreat_ = "NEW - DRIFT MINES: THEY ARM WHEN YOU CLOSE, AND THEY CHAIN";
			metMine_ = true;
		}
		else if (!metDrone_ && !drones_.empty())
		{
			newThreat_ = "NEW - HUNTER DRONES: THEY RAM. THE FIELD SWATS THEM";
			metDrone_ = true;
		}
		else if (!metWell_)
		{
			for (auto& w : wells_)
				if (w.strength > 0)
				{
					newThreat_ = "NEW - SINGULARITY: IT PULLS EVERYTHING, EVEN YOUR SHOTS";
					metWell_ = true;
					break;
				}
		}
		if (newThreat_.empty() && !metRepulsor_)
		{
			for (auto& w : wells_)
				if (w.strength < 0)
				{
					newThreat_ = "NEW - REPULSOR: RIDE IT FOR SPEED, OR IT SPITS YOU AT THE ROCK";
					metRepulsor_ = true;
					break;
				}
		}

		reactorPos_ = field_.reactorPos;
		reactorHits_ = 0;
		reactorDead_ = false;
		meltdownVis_ = 0;
		turretStun_ = 0;

		podAttached_ = false;
		podDelivered_ = false;
		podPos_ = field_.orbPos;
		tetherTension_ = 0;

		shipPos_ = field_.startPos;
		shipVel_ = {0, 0};
		shipAng_ = 0;
		shipAngVel_ = 0;
		fuel_ = FUEL_MAX;
		shield_ = SHIELD_MAX;
		shieldLock_ = 0;
		shieldFlash_ = 0;
		shieldProx_ = 0;
		dilation_ = 100;
		invuln_ = 1.6f;
		gunCooldown_ = 0;
		thrustVis_ = retroVis_ = shieldVis_ = 0;
		accum_ = 0;

		run_ = RunStats();
		failReason_ = "";

		bullets_.clear();
		parts_.clear();
		shards_.clear();

		camPos_ = shipPos_;
		modeTime_ = 0;
		mode_ = Mode::Briefing;
	}

	void Game::Message(const char* text, float seconds)
	{
		msg_ = text;
		msgTime_ = seconds;
	}

	// ------------------------------------------------------------------- frame

	void Game::SetView(int width, int height)
	{
		viewW_ = std::max(1, width);
		viewH_ = std::max(1, height);
	}

	bool Game::TakeFullscreenRequest()
	{
		const bool wanted = wantFullscreen_;
		wantFullscreen_ = false;
		return wanted;
	}

	bool Game::TakeScreenshotRequest()
	{
		const bool wanted = wantScreenshot_;
		wantScreenshot_ = false;
		return wanted;
	}

	void Game::ScreenshotSaved(bool saved)
	{
		if (saved) Message("SCREENSHOT SAVED", 1.5f);
	}

	void Game::Step(float dt)
	{
		dt = std::min(dt, 0.05f);
		time_ += dt;

		if (win_->Pressed(KEY_FULLSCREEN)) wantFullscreen_ = true;
		if (win_->Pressed(KEY_MUTE))
		{
			audio_->SetMuted(!audio_->Muted());
			audio_->Play(SFX_UI);
		}
		// The manual is only ever drawn over a mission. Toggled anywhere else it
		// would stay armed unseen and pop up over the next briefing.
		const bool inMission = mode_ == Mode::Briefing || mode_ == Mode::Playing || mode_ == Mode::Dying;
		if (!inMission) showHelp_ = false;
		else if (win_->Pressed(KEY_HELP)) showHelp_ = !showHelp_;

		// Escape ladders out one step at a time: flying -> paused -> star map -> quit.
		if (win_->Pressed(KEY_QUIT))
		{
			if (mode_ == Mode::Playing && !paused_)
			{
				paused_ = true;
			}
			else if (mode_ == Mode::Playing || mode_ == Mode::Briefing || mode_ == Mode::Dying ||
			         mode_ == Mode::Results)
			{
				paused_ = false;
				showHelp_ = false;
				mode_ = Mode::Grid;
				modeTime_ = 0;
				audio_->SetMusic(0, spec_.seed);
			}
			else if (mode_ == Mode::Grid)
			{
				mode_ = Mode::Title;
				modeTime_ = 0;
			}
			else
			{
				quit_ = true;
			}
		}
		if (win_->Pressed(KEY_PAUSE) && mode_ == Mode::Playing) paused_ = !paused_;

		modeTime_ += dt;
		msgTime_ = std::max(0.0f, msgTime_ - dt);

		switch (mode_)
		{
		case Mode::Title:
		{
			camDist_ = 95.0f;
			float halfH = Renderer::kViewSlope * camDist_;
			float halfW = halfH * Aspect();
			camPos_.x += 7.0f * dt;
			if (camPos_.x > field_.Max().x - halfW) camPos_.x = field_.Min().x + halfW;
			camPos_.y = lerpf(camPos_.y, field_.startPos.y - 22.0f, damp(1.5f, dt));
			if (win_->Pressed(KEY_START) || win_->Pressed(KEY_FIRE))
			{
				audio_->Play(SFX_UI);
				mode_ = Mode::Grid;
				modeTime_ = 0;
			}
			break;
		}

		case Mode::Grid: UpdateGrid(); break;

		case Mode::Briefing:
			camPos_ = lerp(camPos_, shipPos_, damp(3.0f, dt));
			camDist_ = lerpf(camDist_, 62.0f, damp(2.0f, dt));
			if (modeTime_ > 0.7f && (win_->Pressed(KEY_START) || win_->Pressed(KEY_FIRE) || modeTime_ > 5.0f))
			{
				mode_ = Mode::Playing;
				modeTime_ = 0;
				audio_->SetMusic(1, spec_.seed);
				Message(spec_.reactorHits > 0 ? "BREAK THE FACTORY, LIFT THE ORB, CLIMB OUT"
				                              : "LIFT THE ORB AND CLIMB OUT",
				        3.0f);
			}
			break;

		case Mode::Playing:
			// Reading the manual should not cost you a planet.
			if (!paused_ && !showHelp_)
			{
				slowMo_ = win_->Down(KEY_SLOW) && dilation_ > 1.0f;
				timeScale_ = slowMo_ ? 0.40f : 1.0f;
				dilation_ = clampf(dilation_ + (slowMo_ ? -34.0f : 13.0f) * dt, 0.0f, 100.0f);

				float scaled = dt * timeScale_;
				accum_ += scaled;
				int steps = 0;
				tractorPress_ = win_->Pressed(KEY_TRACTOR);
				while (accum_ >= FIXED_DT && steps < 8)
				{
					Simulate(FIXED_DT);
					accum_ -= FIXED_DT;
					++steps;
				}
				tractorPress_ = false;
				UpdateCamera(dt * timeScale_, dt);
			}
			// One life per planet, so the retry has to cost nothing but a key.
			if (win_->Pressed(KEY_RESTART)) StartPlanet(spec_.index);
			break;

		case Mode::Dying:
			UpdateParticles(dt);
			UpdateCamera(dt, dt);
			if (modeTime_ > 2.0f) FinishRun(false);
			break;

		case Mode::Results:
			UpdateParticles(dt);
			camDist_ = lerpf(camDist_, 105.0f, damp(1.2f, dt));
			if (win_->Pressed(KEY_RESTART))
			{
				StartPlanet(spec_.index);
				break;
			}
			if (modeTime_ > 0.5f && (win_->Pressed(KEY_START) || win_->Pressed(KEY_FIRE)))
			{
				mode_ = Mode::Grid;
				modeTime_ = 0;
				audio_->SetMusic(0, spec_.seed);
			}
			break;
		}

		UpdateAudio();
		shakeOffset_ = shakeRng_.inDisc() * (shake_ * SHAKE_DISTANCE);
		if (win_->Pressed(KEY_SHOT)) wantScreenshot_ = true;
	}

	void Game::UpdateGrid()
	{
		const int c = gridSel_ % kPlanetCols, r = gridSel_ / kPlanetCols;
		int nc = c, nr = r;
		if (win_->Pressed(KEY_LEFT)) nc = (c + kPlanetCols - 1) % kPlanetCols;
		if (win_->Pressed(KEY_RIGHT)) nc = (c + 1) % kPlanetCols;
		if (win_->Pressed(KEY_UP)) nr = std::max(0, r - 1);
		if (win_->Pressed(KEY_DOWN)) nr = std::min(kPlanetRows - 1, r + 1);
		if (nc != c || nr != r)
		{
			gridSel_ = nr * kPlanetCols + nc;
			audio_->Play(SFX_UI, 1.0f + (r - nr) * 0.08f, 0.45f);
		}
		if (win_->Pressed(KEY_START) || win_->Pressed(KEY_FIRE))
		{
			if (profile_.Unlocked(gridSel_))
			{
				audio_->Play(SFX_LAUNCH);
				StartPlanet(gridSel_);
			}
			else
			{
				audio_->Play(SFX_HULL_HIT, 0.7f, 0.5f);
			}
		}
	}

	// The score rubric, in one place so the live HUD number and the results screen
	// can never drift apart.
	static void Rubric(bool orbReached, bool orbOut, bool reactorKilled, bool hasReactor, float fuel, float spent,
	                   int relics, int* obj, int* fuelS, int* integ, int* relS)
	{
		int o = 0;
		if (orbReached) o += 15;
		// With no factory to break, the orb carries the whole objective weight.
		if (orbOut) o += hasReactor ? 20 : 35;
		if (hasReactor && reactorKilled) o += 15;
		*obj = o;
		*fuelS = (int)(20.0f * saturatef(fuel / FUEL_MAX) + 0.5f);
		*integ = (int)(20.0f * saturatef(1.0f - spent / SHIELD_BUDGET) + 0.5f);
		*relS = (int)(10.0f * ((float)relics / 3.0f) + 0.5f);
	}

	int Game::LiveScore() const
	{
		int o, fu, in, re;
		Rubric(run_.orbReached, podAttached_, run_.reactorKilled, spec_.reactorHits > 0, fuel_, run_.shieldSpent,
		       run_.relics, &o, &fu, &in, &re);
		return std::min(100, o + fu + in + re);
	}

	// Efficiency and integrity only count on a run that got out, so a crash can
	// never earn a medal - but the objectives you did reach still show.
	void Game::Grade()
	{
		run_.fuelLeft = fuel_;
		Rubric(run_.orbReached, run_.orbExtracted, run_.reactorKilled, spec_.reactorHits > 0, fuel_, run_.shieldSpent,
		       run_.relics, &run_.objectiveScore, &run_.fuelScore, &run_.integrityScore, &run_.relicScore);

		if (!run_.completed)
		{
			run_.fuelScore = run_.integrityScore = 0;
			run_.objectiveScore /= 2;
			run_.relicScore /= 2;
		}
		run_.percent = std::min(100, run_.objectiveScore + run_.fuelScore + run_.integrityScore + run_.relicScore);
		run_.medal = run_.completed ? MedalFor(run_.percent) : MEDAL_NONE;
	}

	void Game::FinishRun(bool completed)
	{
		run_.completed = completed;
		Grade();
		run_.best = profile_.Submit(spec_.index, run_.percent, run_.relics, completed);
		profile_.Save();
		mode_ = Mode::Results;
		modeTime_ = 0;
		audio_->SetMusic(0, spec_.seed);
	}

	void Game::UpdateAudio()
	{
		bool playing = (mode_ == Mode::Playing && !paused_ && !showHelp_);
		audio_->SetThrust(playing ? std::max(thrustVis_, retroVis_ * 0.6f) * (slowMo_ ? 0.55f : 1.0f) : 0.0f);
		audio_->SetTractor(playing && tractor_ ? (podAttached_ ? 1.0f : 0.55f) : 0.0f,
		                   saturatef(tetherTension_ * 0.5f));

		int intensity = 1;
		if (mode_ == Mode::Title || mode_ == Mode::Grid || mode_ == Mode::Results) intensity = 0;
		else if (mode_ == Mode::Playing && (reactorHits_ > 0 || shield_ < 45.0f)) intensity = 2;
		audio_->SetMusic(intensity, spec_.seed);
	}

	// ---------------------------------------------------------------- simulate

	// Wells are the whole reason the caverns feel alive: the same field bends the
	// player, the pod on its rope, loose mines, drones, every bullet on the screen
	// and the debris. Nothing gets a special case.
	Vec2 Game::WellAccel(Vec2 p) const
	{
		Vec2 a(0, 0);
		for (const auto& w : wells_)
		{
			if (!w.alive) continue;
			Vec2 d = w.pos - p;
			float dist = length(d);
			if (dist > w.radius || dist < 1e-3f) continue;
			float t = dist / w.radius;
			float k = 1.0f - t;
			// Near-inverse falloff with a capped core, so the centre is violent but
			// never numerically explosive.
			float mag = w.strength * k * k / std::max(0.25f, t);
			a += d * (mag * wellBoost_ / dist);
		}
		return a;
	}

	void Game::Simulate(float dt)
	{
		invuln_ = std::max(0.0f, invuln_ - dt);
		turretStun_ = std::max(0.0f, turretStun_ - dt);
		shieldFlash_ = std::max(0.0f, shieldFlash_ - dt * 3.0f);
		// A failing reactor drags the fields with it.
		wellBoost_ = 1.0f + meltdownVis_ * (0.55f + 0.45f * sinf(time_ * 2.3f));
		for (auto& w : wells_) w.flare = std::max(0.0f, w.flare - dt * 3.0f);

		UpdateShip(dt);
		UpdateRope(dt);

		// The wells bend every rigid body, not just the player. Nothing gets a
		// special case, which is why they read as part of the world.
		if (!wells_.empty())
		{
			for (int i = 0; i < world_.BodyCount(); ++i)
			{
				if (!world_.Valid(i)) continue;
				Body& b = world_[i];
				if (b.Static()) continue;
				Vec2 a = WellAccel(b.pos);
				if (lengthSq(a) > 1e-4f)
				{
					b.force += a * b.mass;
					world_.Wake(i);
				}
			}
		}

		world_.Step(dt);

		Body& sb = world_[shipBody_];
		sb.ang = shipAng_;
		shipPos_ = sb.pos;
		shipVel_ = sb.vel;
		podPos_ = world_[podBody_].pos;

		// Everything the ship ran into this step, converted into shield cost. Rock,
		// crates, boulders and tanks all arrive through the same door.
		if (mode_ == Mode::Playing && sb.impact > IMPACT_FREE)
		{
			float cost = (sb.impact - IMPACT_FREE) * IMPACT_SCALE;
			Vec2 n = sb.impactNormal;
			Emit(sb.impactPos, n * 12.0f, (int)clampf(cost * 0.6f, 3, 26), Vec3(0.8f, 2.2f, 3.2f),
			     Vec3(0.2f, 0.4f, 0.9f), 0.4f, 1.2f, 0.15f, 0.5f, 1.6f, 18.0f, false);
			shake_ = std::max(shake_, saturatef(cost / 60.0f) * 0.8f);
			ShieldHit(cost, sb.impactPos, cost > HARD_HIT);
		}

		UpdateProps(dt);
		UpdateBullets(dt);
		UpdateTurrets(dt);
		UpdateMines(dt);
		UpdateDrones(dt);
		UpdateParticles(dt);

		// Fuel cells.
		for (auto& f : fuel_cells_)
		{
			if (!f.alive) continue;
			f.phase += dt * 1.6f;
			float d = length(f.p - shipPos_);
			float grab = tractor_ ? 13.0f : 5.0f;
			if (d < grab)
			{
				if (tractor_ && d > 3.0f) continue; // drawn in by the beam below
				f.alive = false;
				fuel_ = std::min(FUEL_MAX, fuel_ + 34.0f);
				audio_->Play(SFX_PICKUP, rng_.range(0.95f, 1.1f));
				Emit(f.p, {0, 0}, 40, Vec3(0.4f, 2.0f, 1.0f), Vec3(0.1f, 0.4f, 0.6f), 0.5f, 1.6f, 0.3f, 0.9f, kTau,
				     22.0f, false);
				Message("FUEL CELL ABSORBED", 1.4f);
			}
			else if (tractor_ && d < 13.0f)
			{
				f.p += normalize(shipPos_ - f.p) * 26.0f * dt;
			}
		}

		for (auto& rl : relics_)
		{
			if (rl.taken) continue;
			rl.phase += dt * 1.3f;
			if (length(rl.p - shipPos_) < 4.2f)
			{
				rl.taken = true;
				++run_.relics;
				audio_->Play(SFX_PICKUP, 1.35f);
				audio_->Play(SFX_PICKUP, 1.7f, 0.8f, 0.12f);
				Emit(rl.p, {0, 0}, 60, Vec3(2.6f, 2.2f, 0.6f), Vec3(0.8f, 0.4f, 0.1f), 0.5f, 1.8f, 0.3f, 1.0f, kTau,
				     24.0f, false);
				char buf[64];
				sprintf_s(buf, "RELIC RECOVERED  %d/%d", run_.relics, (int)relics_.size());
				Message(buf, 2.0f);
			}
		}

		// The factory going up leaves the caverns unsettled for a few seconds, but
		// nothing is counting down - running dry is the only way to lose time.
		meltdownVis_ = lerpf(meltdownVis_, 0.0f, damp(0.7f, dt));

		// Escape check. Climbing out without the orb is abandoning the mission, not
		// finishing it - otherwise flying straight up off the spawn scores a medal.
		if (mode_ == Mode::Playing && shipPos_.y > field_.escapeY)
		{
			if (!podAttached_)
			{
				failReason_ = "ABANDONED - THE ORB IS STILL DOWN THERE";
				audio_->Play(SFX_UNLATCH, 0.7f);
				FinishRun(false);
				return;
			}
			run_.orbExtracted = true;
			podDelivered_ = true;
			audio_->Play(SFX_LAUNCH, 1.0f, 1.0f);
			audio_->Play(SFX_PICKUP, 1.0f, 0.9f, 0.15f);
			audio_->Play(SFX_PICKUP, 1.25f, 0.9f, 0.35f);
			audio_->Play(SFX_PICKUP, 1.5f, 0.9f, 0.55f);
			FinishRun(true);
		}
	}

	void Game::UpdateShip(float dt)
	{
		if (mode_ != Mode::Playing) return;
		Body& b = world_[shipBody_];

		float turn = (win_->Down(KEY_RIGHT) ? 1.0f : 0.0f) - (win_->Down(KEY_LEFT) ? 1.0f : 0.0f);
		shipAngVel_ = lerpf(shipAngVel_, -turn * ANG_RATE, damp(22.0f, dt));
		shipAng_ += shipAngVel_ * dt;
		b.ang = shipAng_;

		Vec2 up = upFromAngle(shipAng_);

		thrusting_ = win_->Down(KEY_UP) && fuel_ > 0.0f;
		retro_ = win_->Down(KEY_DOWN) && fuel_ > 0.0f && !thrusting_;
		overcharge_ = win_->Down(KEY_SHIELD) && fuel_ > 0.0f;
		tractor_ = win_->Down(KEY_TRACTOR);

		if (thrusting_)
		{
			// Hauling the orb diverts reactor power to the main drive.
			float power = THRUST_ACC * (podAttached_ ? 1.45f : 1.0f);
			b.force += up * (power * b.mass);
			fuel_ -= (podAttached_ ? 8.0f : 6.6f) * dt;
			thrustVis_ = std::min(1.0f, thrustVis_ + dt * 6.0f);
		}
		else
		{
			thrustVis_ = std::max(0.0f, thrustVis_ - dt * 5.0f);
		}
		// The retro is deliberately weaker than the main drive. Flip and burn stays
		// the fast way; nudging backwards stays the safe one.
		if (retro_)
		{
			b.force -= up * (RETRO_ACC * b.mass);
			fuel_ -= 4.2f * dt;
			retroVis_ = std::min(1.0f, retroVis_ + dt * 7.0f);
		}
		else
		{
			retroVis_ = std::max(0.0f, retroVis_ - dt * 6.0f);
		}
		if (tractor_ || podAttached_) fuel_ -= 1.2f * dt;

		// The field comes back on its own, paid for out of the same tank that runs
		// the engines. A heavy knock puts it out for a moment first.
		shieldLock_ = std::max(0.0f, shieldLock_ - dt);
		if (shieldLock_ <= 0.0f && shield_ < SHIELD_MAX && fuel_ > 0.0f)
		{
			float rate = SHIELD_RECHARGE * (overcharge_ ? 2.8f : 1.0f);
			float add = std::min(rate * dt, SHIELD_MAX - shield_);
			float cost = add * SHIELD_FUEL_COST * (overcharge_ ? 1.7f : 1.0f);
			if (cost > fuel_)
			{
				add *= fuel_ / std::max(cost, 1e-4f);
				cost = fuel_;
			}
			shield_ += add;
			fuel_ -= cost;
		}
		// Wall repulsion, one over distance squared. The last couple of metres are
		// a cushion you can feel rather than a cliff you fall off, and holding the
		// ship against rock costs field - which is why hugging a wall is expensive.
		shieldProx_ = std::max(0.0f, shieldProx_ - dt * 3.0f);
		if (shieldLock_ <= 0.0f && shield_ > 1.0f)
		{
			float sdf = field_.Sample(shipPos_);
			Vec2 g = field_.Gradient(shipPos_);
			float gl = length(g);
			if (gl > 1e-4f)
			{
				const float reach = 7.5f;
				float gap = std::max(-sdf / gl - SHIP_RADIUS, 0.45f);
				if (gap < reach)
				{
					float k = std::min((reach * reach) / (gap * gap) - 1.0f, 90.0f);
					b.force += (g * (-1.0f / gl)) * (k * 1.6f * b.mass);
					shieldProx_ = std::max(shieldProx_, saturatef(k * 0.06f));
					shield_ = std::max(0.0f, shield_ - saturatef(k * 0.05f) * 3.0f * dt);
				}
			}
		}

		// Almost invisible until it does something. A field you can always see is
		// wallpaper; one that flares on a hit is information.
		float bubble = shieldLock_ > 0.0f
		                   ? 0.0f
		                   : 0.045f + shieldFlash_ * 1.10f + shieldProx_ * 0.90f + (overcharge_ ? 0.65f : 0.0f);
		shieldVis_ = lerpf(shieldVis_, bubble, damp(14.0f, dt));

		// Emergency reserve: never leaves the player completely stranded.
		if (fuel_ < 10.0f) fuel_ = std::min(10.0f, fuel_ + 1.8f * dt);
		fuel_ = clampf(fuel_, 0.0f, FUEL_MAX);

		// Soft terminal velocity. A hard clamp reads as an invisible wall; drag
		// that ramps in reads as air, and warns you before the ceiling arrives.
		float sp = length(b.vel);
		if (sp > SOFT_SPEED)
		{
			float over = (sp - SOFT_SPEED) / (MAX_SPEED - SOFT_SPEED);
			b.force -= b.vel * (over * over * 55.0f * b.mass / std::max(sp, 1e-3f));
		}
		if (sp > MAX_SPEED * 1.4f) b.vel *= MAX_SPEED * 1.4f / sp;

		gunCooldown_ -= dt;
		if (win_->Down(KEY_FIRE) && gunCooldown_ <= 0)
		{
			gunCooldown_ = 0.17f;
			Bullet bl;
			bl.p = shipPos_ + up * 2.2f;
			bl.v = up * BULLET_SPEED + shipVel_ * 0.35f;
			bl.life = 1.8f;
			bl.player = true;
			bl.hue = 0.12f;
			bullets_.push_back(bl);
			b.vel -= up * 1.1f;
			audio_->Play(SFX_SHOT, rng_.range(0.94f, 1.08f), 0.9f);
			Emit(bl.p, up * 6.0f, 6, Vec3(3.0f, 2.4f, 1.2f), Vec3(1.0f, 0.3f, 0.1f), 0.4f, 0.9f, 0.06f, 0.18f, 0.7f,
			     10.0f, false);
		}

		float plume = std::max(thrustVis_, retroVis_);
		if (plume > 0.02f)
		{
			bool main = thrustVis_ >= retroVis_;
			Vec2 out = main ? -up : up;
			Vec2 tail = shipPos_ + out * 1.25f;
			int n = 1 + (int)(plume * 5.0f);
			for (int i = 0; i < n; ++i)
			{
				Vec2 dir = out + perp(up) * rng_.signed1() * 0.28f;
				Particle p;
				p.p = tail + rng_.inDisc() * 0.5f;
				p.v = shipVel_ + normalize(dir) * rng_.range(20.0f, 46.0f) * plume;
				p.maxLife = rng_.range(0.22f, 0.5f);
				p.life = p.maxLife;
				p.size = rng_.range(0.30f, 0.85f) * (main ? 1.0f : 0.65f);
				p.growth = 0.8f;
				p.col0 = main ? Vec3(1.10f, 1.55f, 2.10f) : Vec3(1.60f, 1.20f, 2.20f);
				p.col1 = Vec3(0.75f, 0.10f, 0.48f);
				p.drag = 2.6f;
				p.gravity = 0.15f;
				p.collide = true;
				p.z = rng_.range(-0.7f, 0.7f);
				p.stretch = 0.7f;
				p.type = 0;
				if (parts_.size() < 13000) parts_.push_back(p);
			}
		}
	}

	// ------------------------------------------------------------------- orb

	// A single distance constraint is a stick. A short chain drapes, whips and
	// wraps, and the swing is the whole point of hauling the thing.
	void Game::LatchOrb()
	{
		if (podAttached_) return;
		podAttached_ = true;
		run_.orbReached = true;
		ropeLinks_.clear();
		ropeJoints_.clear();

		const float seg = TETHER_LEN / (ROPE_LINKS + 1);
		int prev = shipBody_;
		Vec2 prevLocal(0, -1.1f);
		for (int i = 0; i < ROPE_LINKS; ++i)
		{
			Body lb;
			lb.pos = lerp(shipPos_, podPos_, (float)(i + 1) / (ROPE_LINKS + 1));
			lb.shape = MakeCircle(0.55f);
			lb.restitution = 0.02f;
			lb.friction = 0.30f;
			lb.linDamp = 0.35f;
			lb.flags = BF_NO_SLEEP;
			int id = world_.AddBody(lb, 0.18f);
			ropeLinks_.push_back(id);
			ropeJoints_.push_back(world_.AddDistance(prev, id, prevLocal, Vec2(0, 0), seg, true, 0));
			prev = id;
			prevLocal = Vec2(0, 0);
		}
		ropeJoints_.push_back(world_.AddDistance(prev, podBody_, prevLocal, Vec2(0, 0), seg, true, 0));

		world_.Wake(podBody_);
		audio_->Play(SFX_LATCH);
		Message("ORB TETHERED - IT SWINGS, SO FLY LIKE IT DOES", 2.4f);
		Emit(podPos_, {0, 0}, 50, Vec3(2.4f, 1.2f, 3.0f), Vec3(0.5f, 0.1f, 0.8f), 0.5f, 1.8f, 0.3f, 0.9f, kTau, 20.0f,
		     false);
	}

	void Game::ReleaseOrb()
	{
		if (!podAttached_) return;
		podAttached_ = false;
		for (int j : ropeJoints_) world_.RemoveJoint(j);
		for (int id : ropeLinks_) world_.RemoveBody(id);
		ropeJoints_.clear();
		ropeLinks_.clear();
		audio_->Play(SFX_UNLATCH);
		Message("ORB RELEASED", 1.5f);
	}

	void Game::UpdateRope(float dt)
	{
		podSpin_ += dt * 1.1f;
		if (mode_ != Mode::Playing) return;

		float d = length(podPos_ - shipPos_);
		if (!podAttached_)
		{
			if (tractor_ && d < 17.0f)
			{
				Body& ob = world_[podBody_];
				ob.force += normalize(shipPos_ - podPos_) * (22.0f * ob.mass);
				world_.Wake(podBody_);
				if (d < 8.5f) LatchOrb();
			}
			tetherTension_ = lerpf(tetherTension_, 0.0f, damp(6.0f, dt));
			return;
		}

		// Latched is a state, not a key you hold. Hauling the orb up a shaft is
		// already work; making the player pin a key down for it is not.
		if (tractorPress_)
		{
			ReleaseOrb();
			return;
		}
		float load = 0;
		for (int j : ropeJoints_)
			if (j >= 0 && j < world_.JointCount()) load = std::max(load, world_.JointAt(j).load);
		tetherTension_ = lerpf(tetherTension_, saturatef(load / 26.0f), damp(8.0f, dt));
	}

	// ----------------------------------------------------------------- props

	int Game::PropAt(Vec2 p, float radius) const
	{
		for (size_t i = 0; i < props_.size(); ++i)
		{
			const Prop& pr = props_[i];
			if (!pr.alive || !world_.Valid(pr.body)) continue;
			const Body& b = world_[pr.body];
			float r = b.shape.kind == SHAPE_CIRCLE ? b.shape.radius : b.shape.bound * 0.8f;
			float sum = r + radius;
			if (lengthSq(p - b.pos) < sum * sum) return (int)i;
		}
		return -1;
	}

	void Game::HurtProp(Prop& p, float dmg, Vec2 at)
	{
		if (!p.alive || dmg <= 0) return;
		p.hp -= dmg;
		p.flash = 1.0f;
		if (p.hp > 0) return;
		// Tanks always go off through the fuse, so a chain reaction can never
		// recurse through Blast.
		if (p.kind == PROP_TANK)
		{
			if (p.fuse < 0) p.fuse = 0.0f;
		}
		else
		{
			DestroyProp(p, true);
		}
		(void)at;
	}

	void Game::DestroyProp(Prop& p, bool violent)
	{
		if (!p.alive) return;
		p.alive = false;
		Vec2 at = world_.Valid(p.body) ? world_[p.body].pos : p.anchorA;
		float r = p.radius;

		if (p.jointA >= 0) world_.RemoveJoint(p.jointA);
		if (p.jointB >= 0) world_.RemoveJoint(p.jointB);
		p.jointA = p.jointB = -1;
		world_.RemoveBody(p.body);
		p.body = -1;

		if (p.kind == PROP_TANK)
		{
			audio_->Play(SFX_EXPLODE_SMALL, rng_.range(0.62f, 0.82f), 1.0f);
			Blast(at, 12.0f + r * 4.0f, 36.0f, Vec3(2.8f, 1.3f, 0.3f));
		}
		else
		{
			Explode(at, violent ? 0.9f : 0.5f, Vec3(1.1f, 1.2f, 1.6f), false);
			audio_->Play(SFX_EXPLODE_SMALL, rng_.range(1.1f, 1.35f), 0.55f);
		}
	}

	void Game::UpdateProps(float dt)
	{
		for (auto& p : props_)
		{
			if (!p.alive) continue;
			p.flash = std::max(0.0f, p.flash - dt * 3.0f);

			if (p.fuse >= 0)
			{
				p.fuse -= dt;
				if (p.fuse <= 0)
				{
					DestroyProp(p, true);
					continue;
				}
			}
			if (!world_.Valid(p.body)) continue;
			Body& b = world_[p.body];

			float threshold = p.kind == PROP_TANK ? 26.0f : 55.0f;
			if (b.impact > threshold)
			{
				HurtProp(p, (b.impact - threshold) * 0.22f, b.impactPos);
				Emit(b.impactPos, b.impactNormal * 8.0f, 4, Vec3(1.4f, 1.2f, 1.0f), Vec3(0.4f, 0.35f, 0.3f), 0.25f,
				     0.7f, 0.2f, 0.6f, 1.4f, 9.0f, true);
			}

			// A leg or a pin letting go is the loudest hint in the game that
			// something is about to move.
			const int js[2] = {p.jointA, p.jointB};
			for (int j : js)
			{
				if (j < 0 || j >= world_.JointCount()) continue;
				if (!world_.JointAt(j).justBroke) continue;
				audio_->Play(SFX_HULL_HIT, rng_.range(1.4f, 1.8f), 0.45f);
				Emit(b.pos, {0, 0}, 10, Vec3(1.8f, 1.5f, 1.1f), Vec3(0.5f, 0.4f, 0.3f), 0.25f, 0.7f, 0.2f, 0.7f, kTau,
				     12.0f, true);
			}
		}
	}

	void Game::UpdateBullets(float dt)
	{
		for (size_t i = 0; i < bullets_.size();)
		{
			Bullet& b = bullets_[i];
			// Shots are massless enough to ignore the planet but not a singularity,
			// so firing across a well means leading the curve.
			b.v += WellAccel(b.p) * 0.9f * dt;
			b.p += b.v * dt;
			b.life -= dt;
			bool dead = b.life <= 0;

			if (!dead && field_.Solid(b.p))
			{
				Vec2 g = field_.Gradient(b.p);
				Vec2 n = length(g) > 1e-4f ? normalize(g) * -1.0f : Vec2(0, 1);
				Emit(b.p, n * 14.0f, 10, Vec3(3.0f, 2.0f, 1.0f), Vec3(0.8f, 0.15f, 0.05f), 0.3f, 0.9f, 0.1f, 0.35f,
				     1.5f, 18.0f, false);
				dead = true;
			}

			if (!dead && b.player)
			{
				int pi = PropAt(b.p, 0.4f);
				if (pi >= 0)
				{
					Prop& pr = props_[(size_t)pi];
					if (world_.Valid(pr.body)) world_.ApplyImpulse(pr.body, normalize(b.v) * 3.0f, b.p);
					HurtProp(pr, 1.5f, b.p);
					Emit(b.p, {0, 0}, 8, Vec3(2.6f, 1.8f, 1.0f), Vec3(0.7f, 0.3f, 0.1f), 0.3f, 0.9f, 0.1f, 0.35f, kTau,
					     14.0f, false);
					dead = true;
				}
				if (!dead)
					for (auto& t : turrets_)
					{
						if (!t.alive) continue;
						if (length(t.pos - b.p) < 2.6f)
						{
							t.alive = false;
							Explode(t.pos, 1.1f, Vec3(2.6f, 0.9f, 0.6f), false);
							audio_->Play(SFX_EXPLODE_SMALL, rng_.range(0.9f, 1.1f));
							AddShockwave(t.pos, 0.5f);
							dead = true;
							break;
						}
					}
				// Popping a mine from a safe distance is the reward for noticing it
				// parked next to something else worth killing.
				if (!dead)
					for (auto& m : mines_)
					{
						if (!m.alive) continue;
						if (length(m.p - b.p) < 2.2f)
						{
							DetonateMine(m);
							dead = true;
							break;
						}
					}
				if (!dead)
					for (auto& d : drones_)
					{
						if (!d.alive) continue;
						if (length(d.p - b.p) < 2.4f)
						{
							d.hp -= 1.0f;
							d.v += normalize(b.v) * 5.0f;
							dead = true;
							if (d.hp <= 0)
							{
								d.alive = false;
								Explode(d.p, 1.3f, Vec3(2.6f, 0.8f, 0.5f), false);
								audio_->Play(SFX_EXPLODE_SMALL, rng_.range(1.0f, 1.2f));
							}
							else
							{
								audio_->Play(SFX_SHIELD_HIT, 1.4f, 0.5f);
								Emit(b.p, {0, 0}, 8, Vec3(3.0f, 1.2f, 0.6f), Vec3(0.8f, 0.2f, 0.1f), 0.3f, 0.9f, 0.1f,
								     0.35f, kTau, 16.0f, false);
							}
							break;
						}
					}
				if (!dead)
					for (auto& w : wells_)
					{
						if (!w.alive) continue;
						if (length(w.pos - b.p) < 3.6f)
						{
							w.hp -= 1.0f;
							w.flare = 1.0f;
							dead = true;
							audio_->Play(SFX_REACTOR_HIT, 1.3f, 0.7f);
							Emit(b.p, {0, 0}, 14, Vec3(2.0f, 1.6f, 3.0f), Vec3(0.4f, 0.2f, 0.9f), 0.35f, 1.2f, 0.15f,
							     0.5f, kTau, 20.0f, false);
							if (w.hp <= 0) KillWell(w);
							break;
						}
					}
				if (!dead && spec_.reactorHits > 0 && !reactorDead_ && length(reactorPos_ - b.p) < 5.0f)
				{
					++reactorHits_;
					turretStun_ = 6.0f;
					dead = true;
					audio_->Play(SFX_REACTOR_HIT, 1.0f + reactorHits_ * 0.05f);
					Emit(b.p, {0, 0}, 30, Vec3(3.0f, 2.0f, 0.6f), Vec3(1.0f, 0.2f, 0.05f), 0.4f, 1.6f, 0.2f, 0.7f, kTau,
					     26.0f, false);
					shake_ = std::max(shake_, 0.5f);
					if (reactorHits_ >= spec_.reactorHits)
					{
						reactorDead_ = true;
						run_.reactorKilled = true;
						// Breaking the factory buys you the rest of the planet in
						// silence. No countdown - fuel is the only clock here.
						turretStun_ = 1.0e9f;
						meltdownVis_ = 1.0f;
						Explode(reactorPos_, 2.4f, Vec3(3.0f, 1.2f, 0.3f), true);
						Blast(reactorPos_, 22.0f, 30.0f, Vec3(2.8f, 1.2f, 0.3f));
						AddShockwave(reactorPos_, 1.0f);
						audio_->Play(SFX_EXPLODE_BIG);
						flash_ = 1.0f;
						flashCol_ = Vec3(1.0f, 0.45f, 0.15f);
						Message("FACTORY DESTROYED - DEFENCES DEAD", 4.0f);
					}
					else
					{
						char buf[96];
						sprintf_s(buf, "FACTORY %d/%d - DEFENCES OFFLINE", reactorHits_, spec_.reactorHits);
						Message(buf, 2.0f);
					}
				}
			}
			else if (!dead)
			{
				// Turrets have no idea what is floating in front of them.
				for (auto& m : mines_)
				{
					if (!m.alive) continue;
					if (length(m.p - b.p) < 2.2f)
					{
						DetonateMine(m);
						dead = true;
						break;
					}
				}
				if (!dead)
				{
					int pi = PropAt(b.p, 0.4f);
					if (pi >= 0)
					{
						HurtProp(props_[(size_t)pi], 1.2f, b.p);
						dead = true;
					}
				}
				float rad = shieldLock_ > 0 ? SHIP_RADIUS + 0.5f : 3.2f;
				if (!dead && length(b.p - shipPos_) < rad && invuln_ <= 0)
				{
					Vec2 n = normalize(b.p - shipPos_);
					bool live = shieldLock_ <= 0.0f && shield_ > BULLET_SHIELD_COST + 1.0f;
					if (live && dot(b.v, n) < 0.0f)
					{
						// A field deflects; it does not swallow. The shot comes back
						// out still lethal, and can kill whatever fired it.
						b.p = shipPos_ + n * (rad + 0.4f);
						b.v = b.v - n * (2.0f * dot(b.v, n));
						b.player = true;
						b.hue = 0.45f;
						b.life = std::max(b.life, 1.4f);
						audio_->Play(SFX_SHIELD_HIT, rng_.range(1.25f, 1.55f), 0.85f);
						Emit(b.p, n * 18.0f, 12, Vec3(0.9f, 2.6f, 3.6f), Vec3(0.2f, 0.5f, 1.0f), 0.3f, 0.9f, 0.1f,
						     0.35f, 1.3f, 22.0f, false);
						ShieldHit(BULLET_SHIELD_COST, b.p, false);
					}
					else
					{
						dead = true;
						Emit(b.p, n * 14.0f, 10, Vec3(2.8f, 1.2f, 0.5f), Vec3(0.8f, 0.2f, 0.1f), 0.3f, 0.9f, 0.1f,
						     0.35f, 1.6f, 16.0f, false);
						ShieldHit(BULLET_SHIELD_COST, b.p, false);
					}
				}
			}

			if (dead)
			{
				bullets_[i] = bullets_.back();
				bullets_.pop_back();
			}
			else
			{
				++i;
			}
		}
	}

	bool Game::LineOfSight(Vec2 a, Vec2 b) const
	{
		Vec2 d = b - a;
		float len = length(d);
		if (len < 1e-3f) return true;
		Vec2 step = d / len * 3.0f;
		Vec2 p = a + step;
		// t walks the ray in exact steps of 3 units.
		for (float t = 3.0f; t < len - 2.0f; t += 3.0f, p += step)
			if (field_.Solid(p)) return false;
		return true;
	}

	void Game::UpdateTurrets(float dt)
	{
		if (mode_ != Mode::Playing) return;
		for (auto& t : turrets_)
		{
			if (!t.alive) continue;
			Vec2 toShip = shipPos_ - t.pos;
			float d = length(toShip);
			float want = t.baseAngle;
			bool canSee = d < 165.0f && LineOfSight(t.pos + t.normal * 2.0f, shipPos_);

			if (canSee)
			{
				float travel = d / std::max(spec_.bulletSpeed, 1.0f);
				Vec2 lead = shipPos_ + shipVel_ * travel * 0.75f;
				want = atan2f(lead.y - t.pos.y, lead.x - t.pos.x);
				float rel = wrapAngle(want - t.baseAngle);
				rel = clampf(rel, -1.35f, 1.35f); // turrets cannot shoot through their own rock
				want = t.baseAngle + rel;
			}
			t.aim += wrapAngle(want - t.aim) * damp(6.0f, dt);
			t.tracking = canSee && turretStun_ <= 0;

			t.cooldown -= dt;
			if (canSee && turretStun_ <= 0 && t.cooldown <= 0 && fabsf(wrapAngle(t.aim - want)) < 0.2f)
			{
				t.cooldown = spec_.fireInterval * rng_.range(0.8f, 1.25f);
				Bullet b;
				Vec2 dir = fromAngle(t.aim);
				b.p = t.pos + dir * 3.0f;
				b.v = dir * spec_.bulletSpeed;
				b.life = 3.0f;
				b.player = false;
				b.hue = 0.95f;
				bullets_.push_back(b);
				audio_->Play(SFX_ENEMY_SHOT, rng_.range(0.9f, 1.1f), saturatef(1.0f - d / 200.0f) * 0.9f);
				Emit(b.p, dir * 8.0f, 5, Vec3(3.0f, 0.8f, 1.2f), Vec3(0.6f, 0.05f, 0.2f), 0.3f, 0.8f, 0.06f, 0.18f,
				     0.6f, 8.0f, false);
			}
		}
	}

	// A blast is the shared currency between mines, dying drones and dying wells:
	// it hurts, it shoves, and it sets off whatever else is lying around.
	void Game::Blast(Vec2 at, float radius, float damage, Vec3 col)
	{
		Explode(at, radius * 0.14f, col, radius > 13.0f);

		for (auto& t : turrets_)
		{
			if (!t.alive) continue;
			if (length(t.pos - at) < radius * 0.75f)
			{
				t.alive = false;
				Explode(t.pos, 1.1f, Vec3(2.6f, 0.9f, 0.6f), false);
				audio_->Play(SFX_EXPLODE_SMALL, rng_.range(0.85f, 1.0f));
			}
		}
		for (auto& d : drones_)
		{
			if (!d.alive) continue;
			Vec2 delta = d.p - at;
			float dist = length(delta);
			if (dist > radius) continue;
			float k = 1.0f - dist / radius;
			d.v += normalize(delta) * (radius * 4.0f * k);
			d.hp -= damage * k * 0.1f;
			if (d.hp <= 0)
			{
				d.alive = false;
				Explode(d.p, 1.2f, Vec3(2.6f, 0.7f, 0.5f), false);
				audio_->Play(SFX_EXPLODE_SMALL, rng_.range(1.0f, 1.15f));
			}
		}
		for (auto& m : mines_)
		{
			if (!m.alive) continue;
			float dist = length(m.p - at);
			if (dist > radius * 1.5f || dist < 1e-3f) continue;
			m.v += normalize(m.p - at) * (radius * 2.5f * (1.0f - dist / (radius * 1.5f)));
			// Sympathetic detonation, staggered so a cluster ripples instead of
			// firing as one indistinguishable bang.
			if (m.fuse <= 0 || m.fuse > 0.22f) m.fuse = rng_.range(0.10f, 0.24f);
		}

		// One radial impulse for every rigid body in range: ship, orb, rope links,
		// crates and drums all get shoved by the same call.
		world_.ApplyBlast(at, radius, radius * 5.5f);

		for (auto& p : props_)
		{
			if (!p.alive || !world_.Valid(p.body)) continue;
			float dist = length(world_[p.body].pos - at);
			if (dist > radius * 1.35f) continue;
			float k = 1.0f - dist / (radius * 1.35f);
			// Sympathetic detonation is staggered, so a cluster ripples instead of
			// firing as one indistinguishable bang.
			if (p.kind == PROP_TANK)
			{
				if (p.fuse < 0) p.fuse = rng_.range(0.08f, 0.28f);
			}
			else
			{
				HurtProp(p, damage * k * 0.30f, at);
			}
		}

		float ds = length(shipPos_ - at);
		if (ds < radius && mode_ == Mode::Playing)
		{
			float k = 1.0f - ds / radius;
			float cost = damage * k;
			ShieldHit(cost, shipPos_, cost > HARD_HIT);
		}

		AddShockwave(at, saturatef(radius / 18.0f));
	}

	void Game::DetonateMine(Mine& m)
	{
		m.alive = false;
		audio_->Play(SFX_EXPLODE_SMALL, rng_.range(0.7f, 0.85f), 1.0f);
		const float s = spec_.mineStrength;
		Blast(m.p, 7.0f + 6.5f * s, 14.0f + 22.0f * s, Vec3(2.6f, 1.3f, 0.3f));
	}

	void Game::UpdateMines(float dt)
	{
		for (auto& m : mines_)
		{
			if (!m.alive) continue;
			m.phase += dt * (1.0f + (m.fuse > 0 ? 6.0f : 0.0f));

			// Almost buoyant, so mines hang where they were laid but a well, a
			// blast or the shield can send them somewhere far more interesting.
			m.v.y -= spec_.gravity * 0.10f * dt;
			m.v += WellAccel(m.p) * 1.35f * dt;
			m.v *= 1.0f - damp(0.55f, dt);

			// Overcharging the field sweeps mines aside and cooks the fuse: a panic
			// button that costs fuel, rather than a wall to hide behind.
			if ((overcharge_ || shieldFlash_ > 0.25f) && mode_ == Mode::Playing)
			{
				Vec2 delta = m.p - shipPos_;
				float dist = length(delta);
				if (dist < 4.4f && dist > 1e-3f)
				{
					m.v += delta * (90.0f * dt / dist);
					m.fuse = std::min(m.fuse <= 0 ? 0.30f : m.fuse, 0.30f);
					Emit(m.p, {0, 0}, 2, Vec3(0.8f, 2.4f, 3.4f), Vec3(0.2f, 0.5f, 1.0f), 0.3f, 0.8f, 0.1f, 0.3f, kTau,
					     12.0f, false);
				}
			}

			float speed = length(m.v);
			if (speed > 34.0f) m.v *= 34.0f / speed;

			Vec2 next = m.p + m.v * dt;
			Vec2 n;
			float depth;
			if (field_.SphereHit(next, 1.4f, &n, &depth))
			{
				next += n * (depth + 0.01f);
				float vn = dot(m.v, n);
				if (vn < 0) m.v -= n * (vn * 1.5f);
				m.v *= 0.6f;
			}
			m.p = next;

			if (mode_ != Mode::Playing) continue;

			float d = length(shipPos_ - m.p);
			// A young mine notices you late and gives you longer to leave.
			const float armAt = 10.0f + 7.0f * spec_.mineStrength;
			const float fuseLen = 1.55f - 0.50f * spec_.mineStrength;
			if (m.fuse <= 0 && d < armAt)
			{
				m.fuse = fuseLen;
				audio_->Play(SFX_TICK, 1.5f, saturatef(1.0f - d / 40.0f) * 0.8f);
			}
			if (m.fuse > 0)
			{
				float before = m.fuse;
				m.fuse -= dt;
				// Beep rate doubles over the fuse so the ear tracks the countdown.
				float rate = lerpf(4.0f, 14.0f, 1.0f - saturatef(m.fuse / fuseLen));
				if (floorf(before * rate) != floorf(m.fuse * rate))
					audio_->Play(SFX_TICK, 1.7f + (1.0f - m.fuse) * 0.6f, saturatef(1.0f - d / 45.0f) * 0.55f);
				if (m.fuse <= 0 || d < 2.8f) DetonateMine(m);
			}
		}
	}

	void Game::UpdateDrones(float dt)
	{
		const float ds = spec_.droneStrength;
		const float sightRange = 70.0f + 40.0f * ds;
		for (auto& d : drones_)
		{
			if (!d.alive) continue;

			Vec2 toShip = shipPos_ - d.p;
			float dist = length(toShip);
			bool see = mode_ == Mode::Playing && dist < sightRange && turretStun_ <= 0 && LineOfSight(d.p, shipPos_);
			d.hunting = see;

			Vec2 want;
			if (see)
			{
				Vec2 lead = shipPos_ + shipVel_ * 0.30f;
				want = normalize(lead - d.p);
			}
			else
			{
				d.wander += dt * 0.8f;
				want = normalize(d.home + fromAngle(d.wander) * 15.0f - d.p);
			}

			// Steer off the rock before touching it. Without this they simply grind
			// along the wall and stop reading as pilots.
			float f = field_.Sample(d.p);
			if (f > -11.0f)
			{
				Vec2 g = field_.Gradient(d.p);
				if (length(g) > 1e-4f)
					want = normalize(want + normalize(g) * -1.0f * (2.4f * saturatef(1.0f + f / 11.0f)));
			}

			float acc = see ? (28.0f + 26.0f * ds) : 17.0f;
			d.v += want * (acc * dt);
			d.v += WellAccel(d.p) * 0.85f * dt; // easily dragged into the rock
			d.v *= 1.0f - damp(see ? 1.0f : 1.9f, dt);
			float maxSpeed = see ? (24.0f + 19.0f * ds) : 15.0f;
			float sp = length(d.v);
			if (sp > maxSpeed) d.v *= maxSpeed / sp;
			d.thrust = lerpf(d.thrust, saturatef(sp / maxSpeed), damp(6.0f, dt));
			if (sp > 0.5f) d.ang = atan2f(d.v.y, d.v.x) - kPi * 0.5f;

			Vec2 next = d.p + d.v * dt;
			Vec2 n;
			float depth;
			if (field_.SphereHit(next, 1.5f, &n, &depth))
			{
				next += n * (depth + 0.01f);
				float vn = dot(d.v, n);
				if (vn < 0)
				{
					d.hp -= saturatef(-vn / 26.0f) * 2.2f; // a well can smear them
					d.v -= n * (vn * 1.2f);
				}
				d.v *= 0.7f;
			}
			d.p = next;

			if (mode_ != Mode::Playing) continue;

			// Field contact: a hard swat, and it kills.
			if ((overcharge_ || shieldFlash_ > 0.25f) && dist < 4.2f)
			{
				d.hp -= dt * 9.0f;
				if (dist > 1e-3f) d.v -= normalize(toShip) * (70.0f * dt);
			}
			else if (dist < SHIP_RADIUS + 1.8f && invuln_ <= 0)
			{
				d.hp = 0;
				ShieldHit(8.0f + 13.0f * ds, d.p, false);
				world_.ApplyImpulse(shipBody_, normalize(d.v) * 22.0f, shipPos_);
			}

			if (d.hp <= 0)
			{
				d.alive = false;
				Explode(d.p, 1.3f, Vec3(2.6f, 0.8f, 0.5f), false);
				audio_->Play(SFX_EXPLODE_SMALL, rng_.range(1.0f, 1.2f));
			}
		}
	}

	void Game::KillWell(Well& w)
	{
		w.alive = false;
		audio_->Play(SFX_EXPLODE_BIG, w.strength > 0 ? 1.15f : 0.95f);
		flash_ = std::max(flash_, 0.6f);
		flashCol_ = w.strength > 0 ? Vec3(0.7f, 0.65f, 1.0f) : Vec3(0.6f, 1.0f, 1.0f);
		// Collapsing the emitter dumps the whole field back out as one blast.
		Blast(w.pos, w.radius * 0.42f, 26.0f, Vec3(1.6f, 1.2f, 3.0f));
		Message(w.strength > 0 ? "SINGULARITY COLLAPSED" : "REPULSOR COLLAPSED", 2.0f);
	}

	void Game::UpdateParticles(float dt)
	{
		for (size_t i = 0; i < parts_.size();)
		{
			Particle& p = parts_[i];
			p.life -= dt;
			if (p.life <= 0)
			{
				parts_[i] = parts_.back();
				parts_.pop_back();
				continue;
			}
			p.v.y -= spec_.gravity * p.gravity * dt;
			p.v += WellAccel(p.p) * 0.8f * dt;
			p.v *= 1.0f - damp(p.drag, dt);
			Vec2 next = p.p + p.v * dt;
			if (p.collide && field_.Solid(next))
			{
				Vec2 g = field_.Gradient(next);
				if (length(g) > 1e-4f)
				{
					Vec2 n = normalize(g) * -1.0f;
					p.v = (p.v - n * (2.0f * dot(p.v, n))) * 0.45f;
					p.life *= 0.7f;
					next = p.p + p.v * dt;
				}
			}
			p.p = next;
			++i;
		}

		for (size_t i = 0; i < shards_.size();)
		{
			Shard& s = shards_[i];
			s.life -= dt;
			if (s.life <= 0)
			{
				shards_[i] = shards_.back();
				shards_.pop_back();
				continue;
			}
			s.v.y -= spec_.gravity * 0.55f * dt;
			s.v += WellAccel(s.p) * 0.9f * dt;
			s.v *= 1.0f - damp(0.35f, dt);
			Vec2 next = s.p + s.v * dt;
			if (field_.Solid(next))
			{
				Vec2 g = field_.Gradient(next);
				if (length(g) > 1e-4f)
				{
					Vec2 n = normalize(g) * -1.0f;
					s.v = (s.v - n * (2.0f * dot(s.v, n))) * 0.5f;
					next = s.p + s.v * dt;
				}
			}
			s.p = next;
			s.q = quatMul(quatFromAxisAngle(s.axis, s.spin * dt), s.q);
			++i;
		}

		shake_ = std::max(0.0f, shake_ - dt * 1.6f);
		flash_ = std::max(0.0f, flash_ - dt * 2.2f);
		for (auto& s : shocks_)
			if (s.strength > 0)
			{
				s.t += dt;
				if (s.t > 1.1f) s.strength = 0;
			}
	}

	void Game::UpdateCamera(float /*dt*/, float realDt)
	{
		Vec2 look = shipPos_ + shipVel_ * 0.20f;
		if (podAttached_) look = lerp(look, (shipPos_ + podPos_) * 0.5f, 0.35f);

		float speed = length(shipVel_);
		float target = 46.0f + speed * 0.26f + (podAttached_ ? 6.0f : 0.0f) + meltdownVis_ * 8.0f;
		if (slowMo_) target -= 6.0f;
		camDist_ = lerpf(camDist_, target, damp(2.5f, realDt));

		float halfH = Renderer::kViewSlope * camDist_;
		float halfW = halfH * Aspect();
		Vec2 lo = field_.Min(), hi = field_.Max();
		if (hi.x - lo.x > halfW * 2) look.x = clampf(look.x, lo.x + halfW, hi.x - halfW);
		look.y = clampf(look.y, lo.y + halfH, field_.escapeY + halfH * 0.9f);

		camPos_ = lerp(camPos_, look, damp(7.0f, realDt));
	}

	// ------------------------------------------------------------------ damage

	// The only route damage takes. Everything - rock, bullets, blasts, drones -
	// arrives here as a cost in shield points, and the ship only dies when the
	// field is already flat.
	void Game::ShieldHit(float cost, Vec2 at, bool hard)
	{
		if (mode_ != Mode::Playing || invuln_ > 0 || cost <= 0) return;

		run_.shieldSpent += cost;
		if (shield_ <= 0.5f)
		{
			KillShip(hard ? "HULL CRUSHED" : "FIELD COLLAPSED");
			return;
		}

		shield_ = std::max(0.0f, shield_ - cost);
		shieldFlash_ = 1.0f;
		audio_->Play(SFX_SHIELD_HIT, rng_.range(0.8f, 1.15f), saturatef(0.4f + cost / 60.0f));
		Emit(at, {0, 0}, (int)clampf(cost * 0.5f, 6, 24), Vec3(0.7f, 2.2f, 3.2f), Vec3(0.1f, 0.35f, 0.9f), 0.35f, 1.3f,
		     0.12f, 0.45f, kTau, 18.0f, false);

		// A heavy knock takes the emitter offline. Those seconds are the tensest in
		// the game, and the player generated them.
		if (hard)
		{
			shieldLock_ = LOCKOUT;
			flash_ = std::max(flash_, 0.45f);
			flashCol_ = Vec3(0.5f, 0.8f, 1.2f);
			audio_->Play(SFX_ALARM, 0.7f, 0.7f);
			Message("FIELD OFFLINE - RECHARGING", 1.6f);
		}
		else
		{
			shieldLock_ = std::max(shieldLock_, 0.30f);
		}
		shake_ = std::max(shake_, saturatef(cost / 45.0f));
	}

	void Game::KillShip(const char* reason)
	{
		if (mode_ != Mode::Playing) return;
		failReason_ = reason;
		run_.lost = true;
		mode_ = Mode::Dying;
		modeTime_ = 0;
		ReleaseOrb();
		thrustVis_ = retroVis_ = 0;
		Explode(shipPos_, 2.0f, Vec3(2.6f, 1.6f, 3.2f), true);
		AddShockwave(shipPos_, 1.0f);
		flash_ = 0.8f;
		flashCol_ = Vec3(0.7f, 0.85f, 1.0f);
		audio_->Play(SFX_EXPLODE_BIG, 0.9f);
	}

	void Game::AddShockwave(Vec2 world, float strength)
	{
		int best = 0;
		for (int i = 1; i < 4; ++i)
			if (shocks_[i].strength < shocks_[best].strength) best = i;
		shocks_[best].world = world;
		shocks_[best].t = 0;
		shocks_[best].strength = strength;
	}

	void Game::Emit(Vec2 p, Vec2 v, int count, Vec3 c0, Vec3 c1, float sizeMin, float sizeMax, float lifeMin,
	                float lifeMax, float spread, float speed, bool collide, int type)
	{
		for (int i = 0; i < count; ++i)
		{
			if (parts_.size() >= 13000) return;
			Particle q;
			float a = rng_.signed1() * spread * 0.5f;
			Vec2 dir = length(v) > 1e-3f ? rotate(normalize(v), a) : rng_.unitVec();
			q.p = p + rng_.inDisc() * 0.6f;
			q.v = v * 0.35f + dir * (speed * rng_.range(0.35f, 1.35f));
			q.maxLife = rng_.range(lifeMin, lifeMax);
			q.life = q.maxLife;
			q.size = rng_.range(sizeMin, sizeMax);
			q.growth = 1.1f;
			q.col0 = c0;
			q.col1 = c1;
			q.drag = 1.8f;
			q.gravity = 0.25f;
			q.collide = collide;
			q.z = rng_.range(-1.0f, 1.0f);
			q.stretch = 0.4f;
			q.type = type;
			parts_.push_back(q);
		}
	}

	void Game::Explode(Vec2 p, float scale, Vec3 col, bool big)
	{
		Emit(p, {0, 0}, (int)(60 * scale), col * 1.6f, Vec3(0.6f, 0.1f, 0.05f), 0.6f * scale, 2.6f * scale, 0.25f, 1.0f,
		     kTau, 30.0f * scale, true);
		Emit(p, {0, 0}, (int)(18 * scale), Vec3(3.0f, 2.6f, 2.2f), col, 0.4f * scale, 1.2f * scale, 0.1f, 0.4f, kTau,
		     55.0f * scale, false);

		// Ring puff.
		for (int i = 0; i < (int)(3 * scale); ++i)
		{
			if (parts_.size() >= 13000) break;
			Particle q;
			q.p = p;
			q.v = {0, 0};
			q.maxLife = rng_.range(0.35f, 0.7f);
			q.life = q.maxLife;
			q.size = 2.0f * scale;
			q.growth = 14.0f * scale;
			q.col0 = col * 2.0f;
			q.col1 = col * 0.2f;
			q.drag = 3.0f;
			q.gravity = 0;
			q.collide = false;
			q.type = 1;
			q.z = 0;
			parts_.push_back(q);
		}

		int shardCount = (int)(14 * scale);
		for (int i = 0; i < shardCount; ++i)
		{
			if (shards_.size() >= 1800) break;
			Shard s;
			s.p = p + rng_.inDisc() * scale;
			s.v = rng_.unitVec() * rng_.range(8.0f, 40.0f) * scale;
			s.axis = normalize(Vec3(rng_.signed1(), rng_.signed1(), rng_.signed1() + 0.4f));
			s.spin = rng_.range(-9.0f, 9.0f);
			s.maxLife = rng_.range(0.9f, 2.4f);
			s.life = s.maxLife;
			s.size = rng_.range(0.35f, 1.1f) * scale;
			s.col = col * rng_.range(0.5f, 1.3f);
			shards_.push_back(s);
		}

		if (big)
		{
			shake_ = std::max(shake_, 1.2f * scale);
			AddShockwave(p, 0.8f * scale);
		}
		else
		{
			shake_ = std::max(shake_, 0.35f * scale);
		}
	}

	// ------------------------------------------------------------------ render

	namespace
	{

		// The boundary is the collision line: one bright ink, one constant width.
		const Vec4 kEdge = Hex(0x99F4FF);
		const Vec4 kEdgeHot = Hex(0xFF7733); // the factory going up
		constexpr float kEdgeWidth = 0.54f;  // world units, the same everywhere on the contour
		// A particle smaller than this on screen is one square block.
		constexpr float kBlockPx = 7.0f;
		// Lines either side of a title raster bar's centre.
		constexpr int kBarLines = 12;
		// How fast the star map's sky drifts by at most, in world units a second.
		constexpr float kMapDrift = 3.0f;

		// The HUD's inks, bright on dark glass.
		const Vec4 kCyan = Hex(0x77DDFF);
		const Vec4 kWarm = Hex(0xFFAA44);
		const Vec4 kRed = Hex(0xFF5566);
		const Vec4 kGreen = Hex(0x66EE99);
		const Vec4 kViolet = Hex(0xCC88FF);
		const Vec4 kGold = Hex(0xFFDD55);
		const Vec4 kWhite = Hex(0xFFFFFF);

		// Rotates v by the unit quaternion q.
		Vec3 Rotate(Vec4 q, Vec3 v)
		{
			const Vec3 u(q.x, q.y, q.z);
			const Vec3 t = cross(u, v) * 2.0f;
			return v + t * q.w + cross(u, t);
		}

		// A four pointed star, for sparks that are meant to read as hot.
		void Spark(Renderer& r, Vec2 p, float size, float angle, Vec4 colour)
		{
			Vec2 pts[8];
			for (int i = 0; i < 8; ++i)
			{
				const float rad = (i & 1) ? size * 0.28f : size;
				pts[i] = p + fromAngle(angle + kTau * i / 8.0f) * rad;
			}
			r.Poly(pts, Fill::Solid(colour));
		}

		void DashedRing(Renderer& r, Vec2 c, float radius, int segments, int skipEvery, float phase, float widthPx,
		                Vec4 colour)
		{
			for (int i = 0; i < segments; ++i)
			{
				if ((i % skipEvery) == 0) continue;
				const float a0 = kTau * i / segments + phase, a1 = kTau * (i + 1) / segments + phase;
				r.Line(c + fromAngle(a0) * radius, c + fromAngle(a1) * radius, widthPx, colour);
			}
		}

	} // namespace

	void Game::Draw(render::draw_context& dc, core::recti bounds)
	{
		Renderer& r = r_;
		r.Begin(dc, bounds);

		if (mode_ == Mode::Grid)
		{
			// The star map is drawn on open sky, well above any planet, drifting
			// slowly back and forth across it: the sky is only as wide as the map.
			const float mid = (field_.Min().x + field_.Max().x) * 0.5f;
			const float reach = std::max(1.0f, (field_.Max().x - field_.Min().x) * 0.4f);
			r.SetCamera(Vec2(mid + sinf(time_ * kMapDrift / reach) * reach, terrain_.surfaceY + 140.0f), 60.0f);
			DrawSky(r, terrain_, time_);
			RenderGrid();
			r.End();
			return;
		}

		r.SetCamera(camPos_ + shakeOffset_, camDist_);
		GatherLights();
		TerrainLook look;
		look.time = time_;
		look.meltdown = meltdownVis_;
		look.edge = Mix(kEdge, kEdgeHot, meltdownVis_);
		look.edgeWidth = kEdgeWidth;
		look.lamps = lights_;

		DrawSky(r, terrain_, time_);
		DrawTerrain(r, terrain_, look);
		DrawBoundary(r, terrain_, look);
		RenderWorld();
		RenderDarkness();
		RenderEffects();
		if (flash_ > 0.0f) r.Rect(0, 0, r.Width(), r.Height(), Ocs(flashCol_, flash_ * 0.42f));

		if (mode_ == Mode::Title) RenderTitle();
		else if (mode_ == Mode::Results) RenderResults();
		else RenderHud();
		r.End();
	}

	// There are no fixtures in a mine shaft. Every lamp in the frame belongs to
	// something the player owns, wants or has just set off, which is why an
	// explosion floods the walls and a dead planet is dark.
	void Game::GatherLights()
	{
		lights_.clear();
		if (mode_ == Mode::Grid || mode_ == Mode::Title) return;

		// A lamp whose reach cannot touch the view is not worth a slot.
		auto add = [&](Vec2 p, float radius, Vec3 col)
		{
			if (r_.Visible(p, p, radius)) lights_.push_back({p, radius, col});
		};

		for (auto& s : shocks_)
		{
			if (s.strength <= 0) continue;
			float k = saturatef(1.0f - s.t / 1.1f);
			add(s.world, 34.0f + s.t * 60.0f, Vec3(3.2f, 1.5f, 0.55f) * (k * k * s.strength * 2.4f));
		}

		if (mode_ != Mode::Dying)
		{
			Vec2 plume = shipPos_ - upFromAngle(shipAng_) * 1.7f;
			add(plume, 15.0f + thrustVis_ * 26.0f, Vec3(0.26f, 0.55f, 1.05f) * (0.35f + thrustVis_ * 1.8f));
			if (shieldVis_ > 0.01f) add(shipPos_, 19.0f, Vec3(0.30f, 1.00f, 1.75f) * (shieldVis_ * 1.1f));
		}

		if (!podDelivered_) add(podPos_, 17.0f, Vec3(0.80f, 0.28f, 1.15f) * (0.55f + tetherTension_ * 0.8f));

		if (!reactorDead_ && spec_.reactorHits > 0)
		{
			float dmg = (float)reactorHits_ / spec_.reactorHits;
			add(reactorPos_, 38.0f + dmg * 14.0f,
			    Vec3(1.50f, 0.70f, 0.20f) * (0.85f + dmg * 1.6f + 0.15f * sinf(time_ * 4.0f)));
		}

		for (auto& w : wells_)
		{
			if (!w.alive) continue;
			Vec3 col = w.strength > 0 ? Vec3(0.55f, 0.40f, 1.60f) : Vec3(0.25f, 1.35f, 1.15f);
			add(w.pos, w.radius * 0.85f, col * (0.30f + w.flare));
		}

		for (auto& m : mines_)
		{
			if (!m.alive || m.fuse <= 0) continue;
			float beat = fmodf(m.phase, 1.0f) > 0.42f ? 1.0f : 0.1f;
			add(m.p, 17.0f, Vec3(2.20f, 1.60f, 1.10f) * beat);
		}

		for (auto& p : props_)
		{
			if (!p.alive || p.fuse < 0 || !world_.Valid(p.body)) continue;
			add(world_[p.body].pos, 16.0f, Vec3(2.40f, 1.00f, 0.25f));
		}

		for (auto& rl : relics_)
		{
			if (rl.taken) continue;
			add(rl.p, 12.0f, Vec3(0.95f, 0.72f, 0.22f) * (0.5f + 0.15f * sinf(time_ * 3.4f + rl.phase)));
		}

		for (auto& fc : fuel_cells_)
		{
			if (!fc.alive) continue;
			add(fc.p, 11.0f, Vec3(0.12f, 0.80f, 0.46f) * (0.5f + 0.2f * sinf(time_ * 3.0f + fc.phase)));
		}

		// The budget is small on purpose: a scene lit by everything is lit by
		// nothing, so beyond it the brightest win. Brightest first, too, as the
		// bore only pools the first few.
		auto strength = [](const Lamp& l)
		{
			return std::max(l.colour.x, std::max(l.colour.y, l.colour.z));
		};
		std::stable_sort(lights_.begin(), lights_.end(),
		                 [&](const Lamp& a, const Lamp& b) { return strength(a) > strength(b); });
		if ((int)lights_.size() > kMaxLights) lights_.resize(kMaxLights);
	}

	// On a blackout planet only the ship's own light reaches the rock. Lamps and
	// blasts are drawn after this, so they still burn through the dark.
	void Game::RenderDarkness()
	{
		const bool inMission = mode_ == Mode::Playing || mode_ == Mode::Dying || mode_ == Mode::Briefing;
		const float darkness = spec_.darkness * (inMission ? 1.0f : 0.0f);
		if (darkness <= 0.0f) return;
		const float torch = 62.0f + shieldVis_ * 55.0f + thrustVis_ * 14.0f;
		const float W = r_.Width(), H = r_.Height();
		const Vec2 screen[4] = {{0, 0}, {W, 0}, {W, H}, {0, H}};
		r_.ScreenPoly(screen, Fill::Radial(r_.ToScreen(shipPos_), torch * r_.Scale() * 0.8f, Vec4(0, 0, 0.02f, 0.0f),
		                                   Vec4(0, 0, 0.02f, 0.96f * darkness)));
	}

	void Game::RenderWorld()
	{
		Renderer& r = r_;
		// Everything in the cavern is bolted to the 2D boundary and turned to face
		// out of it, local +y along the wall's normal.
		auto faceWall = [](Vec2 n)
		{
			return atan2f(n.y, n.x) - kPi * 0.5f;
		};
		auto near = [&](Vec2 p, float reach)
		{
			return r.Visible(p, p, reach);
		};
		Look plain;
		plain.rock = Vec4(terrain_.stone, 1.0f);

		for (size_t i = 0; i < fuel_cells_.size(); ++i)
		{
			FuelCell& fc = fuel_cells_[i];
			if (!fc.alive || !near(fc.p, 3.0f)) continue;
			Vec2 n = i < field_.fuelNormal.size() ? field_.fuelNormal[i] : Vec2(0, 1);
			float pulse = 0.6f + 0.4f * sinf(time_ * 3.0f + fc.phase);
			Look l = plain;
			l.accent = Ocs(0.20f + 0.25f * pulse, 0.65f + 0.35f * pulse, 0.40f + 0.2f * pulse);
			DrawFigure(r, fuelFigure_, fc.p, faceWall(n), {1, 1}, l);
			// Stencilled straight off the originals: a tank that says FUEL needs no
			// legend.
			DrawGlyphTextWorld(r, "FUEL", fc.p + n * 0.35f, 0.68f, Ocs(0.85f, 1.0f, 0.85f), 0.13f);
		}

		// The cradle the orb was standing in. It stays behind once you lift it, so
		// the chamber still reads as somewhere something was kept.
		if (near(field_.orbPos, 4.0f))
			DrawFigure(r, pedestalFigure_, field_.orbPos - field_.orbNormal * 1.7f, faceWall(field_.orbNormal), {1, 1},
			           plain);

		for (auto& t : turrets_)
		{
			if (!t.alive || !near(t.pos, 5.0f)) continue;
			bool stunned = turretStun_ > 0;
			float beat = 0.7f + 0.3f * sinf(time_ * (stunned ? 3.0f : 7.0f) + t.pos.x);
			Look l = plain;
			l.accent = stunned ? Ocs(0.25f * beat, 0.45f * beat, 1.0f * beat)
			                   : Ocs(1.0f * beat, 0.25f * beat, 0.3f * beat);
			DrawFigure(r, turretBarrel_, t.pos, t.aim - kPi * 0.5f, {1, 1}, l);
			DrawFigure(r, turretBase_, t.pos, t.baseAngle - kPi * 0.5f, {1, 1}, l);
		}

		// Gravity wells. The emitter is drab on purpose: the field drawn round it is
		// what the player actually has to read.
		for (auto& w : wells_)
		{
			if (!w.alive || !near(w.pos, w.radius)) continue;
			bool pull = w.strength > 0;
			float spin = time_ * (pull ? 1.6f : -1.2f);
			Vec3 tint = pull ? Vec3(0.55f, 0.45f, 1.0f) : Vec3(0.3f, 1.0f, 0.9f);
			r.Glow(w.pos, 4.0f + w.flare * 3.0f + sinf(time_ * 6.0f) * 0.4f, Ocs(tint, 0.45f + 0.3f * w.flare));
			DrawFigure(r, wellRing_, w.pos, spin, {1, 1}, plain);
			float pulse = 1.0f + 0.22f * sinf(time_ * 5.0f) + w.flare * 0.5f;
			Look l = plain;
			l.accent = Ocs(tint * (0.8f + 0.2f * w.flare));
			DrawFigure(r, wellCore_, w.pos, -spin * 1.7f, {pulse, pulse}, l);
		}

		// Drift mines: a slow amber heartbeat, then a hard white strobe on the fuse.
		for (auto& m : mines_)
		{
			if (!m.alive || !near(m.p, 3.0f)) continue;
			bool lit = m.fuse > 0;
			float beat = lit ? (fmodf(m.phase, 1.0f) > 0.42f ? 1.0f : 0.05f) : 0.35f + 0.3f * sinf(m.phase * 2.0f);
			Vec3 hot = lit ? Vec3(1.0f, 0.95f, 0.85f) : Vec3(1.0f, 0.62f, 0.15f);
			r.Glow(m.p, 1.6f + beat * 1.4f, Ocs(hot, 0.35f + 0.4f * beat));
			Look l = plain;
			l.accent = Ocs(hot * (0.35f + 0.65f * beat));
			DrawFigure(r, mineFigure_, m.p, m.phase * 0.3f + m.spin, {1, 1}, l);
		}

		for (auto& d : drones_)
		{
			if (!d.alive || !near(d.p, 3.0f)) continue;
			float eye = d.hunting ? 0.75f + 0.25f * sinf(time_ * 14.0f) : 0.35f;
			Look l = plain;
			l.accent = Ocs(1.0f * eye, 0.35f * eye, 0.25f * eye);
			DrawFigure(r, droneFigure_, d.p, d.ang, {1, 1}, l);
		}

		// Physics props. Every planet has exactly one authored set piece; it is the
		// thing the player is meant to remember, so it is lit to be noticed.
		for (auto& p : props_)
		{
			if (!p.alive || !world_.Valid(p.body)) continue;
			const Body& b = world_[p.body];
			if (!near(b.pos, b.shape.bound + 1.0f)) continue;
			Look l = plain;
			l.flash = saturatef(p.flash * 0.7f + (p.signature ? 0.10f + 0.08f * sinf(time_ * 2.2f + b.pos.x) : 0.0f));
			if (p.kind == PROP_TANK)
			{
				float lit = 0.55f + 0.25f * sinf(time_ * 3.0f + b.pos.x) + p.flash + (p.fuse >= 0 ? 0.6f : 0.0f);
				l.accent = Ocs(1.0f, 0.35f + 0.35f * saturatef(lit), 0.08f + 0.3f * saturatef(lit - 0.6f));
				if (p.fuse >= 0) r.Glow(b.pos, p.radius * 2.6f, Ocs(1.0f, 0.55f, 0.15f, 0.55f));
				DrawFigure(r, tankFigure_, b.pos, b.ang, {p.radius, p.radius}, l);
			}
			else if (p.kind == PROP_BOULDER)
			{
				DrawFigure(r, boulderFigure_, b.pos, b.ang, {p.radius, p.radius}, l);
			}
			else if (p.kind == PROP_CRATE)
			{
				DrawFigure(r, crateFigure_, b.pos, b.ang, {p.halfW, p.halfH}, l);
			}
			else
			{
				DrawFigure(r, slabFigure_, b.pos, b.ang, {p.halfW, p.halfH}, l);
			}
		}

		// Relics turn like a coin on its edge.
		for (size_t i = 0; i < relics_.size(); ++i)
		{
			const Relic& rl = relics_[i];
			if (rl.taken || !near(rl.p, 4.0f)) continue;
			Vec2 n = i < field_.relicNormal.size() ? field_.relicNormal[i] : Vec2(0, 1);
			float pulse = 0.7f + 0.3f * sinf(time_ * 3.4f + rl.phase);
			r.Glow(rl.p, 2.4f + sinf(time_ * 4.0f + rl.phase) * 0.4f, Ocs(1.0f, 0.8f, 0.3f, 0.45f));
			Look l = plain;
			l.accent = Ocs(1.0f * pulse, 0.78f * pulse, 0.22f * pulse);
			DrawFigure(r, relicFigure_, rl.p, faceWall(n), {std::max(0.12f, fabsf(cosf(rl.phase))), 1.0f}, l);
		}

		// Factory: the housing is bolted to the wall, only the core turns.
		if (!reactorDead_ && spec_.reactorHits > 0 && near(reactorPos_, 8.0f))
		{
			float dmg = (float)reactorHits_ / spec_.reactorHits;
			Look house = plain;
			house.flash = dmg * 0.25f;
			DrawFigure(r, factoryFigure_, reactorPos_, faceWall(field_.reactorNormal), {1, 1}, house);
			float pulse = 1.0f + 0.18f * sinf(time_ * (3.0f + dmg * 9.0f));
			Look core = plain;
			core.accent = Ocs(1.0f, 0.62f - dmg * 0.35f, 0.18f);
			DrawFigure(r, factoryCore_, reactorPos_ + field_.reactorNormal * 0.6f, -time_ * 0.35f, {pulse, pulse},
			           core);
		}

		if ((!podDelivered_ || podAttached_) && near(podPos_, 3.0f))
			DrawOrb(r, podPos_, podSpin_, 0.4f + tetherTension_);

		// Ship. One hull, rolling slowly about its own long axis so the fins swing
		// round and it reads as a solid object rather than a card.
		if (mode_ != Mode::Dying && mode_ != Mode::Title && mode_ != Mode::Grid)
		{
			Look l = plain;
			l.alpha = invuln_ > 0 ? (fmodf(time_ * 12.0f, 1.0f) > 0.45f ? 1.0f : 0.35f) : 1.0f;
			float buffet = meltdownVis_ * (0.045f * sinf(time_ * 21.3f) + 0.028f * sinf(time_ * 13.7f));
			DrawShip(r, shipPos_, shipAng_ + buffet, time_ * 0.55f, 1.0f, 0.25f + thrustVis_ * 0.75f + retroVis_ * 0.4f,
			         l);
		}
		if (mode_ == Mode::Title)
		{
			// Hung in front of the slice, so it stands larger than the plane's scale.
			DrawShip(r, camPos_ + Vec2(0.0f, 7.4f), sinf(time_ * 0.6f) * 0.25f, time_ * 0.9f, 4.9f, 0.6f, plain);
		}
	}

	void Game::RenderEffects()
	{
		Renderer& r = r_;
		const float W = r.Width(), H = r.Height();

		for (auto& p : parts_)
		{
			float u = 1.0f - p.life / p.maxLife;
			float alpha = (1.0f - u) * (1.0f - u);
			if (alpha < 0.02f) continue;
			float size = p.size * (1.0f + u * p.growth);
			// Particles drift a little either side of the plane, so they slide by
			// at slightly different rates.
			const Vec2 s = r.ToScreen(p.p, p.z);
			const float px = size * r.ScaleAt(p.z) * 0.8f;
			if (s.x + px < 0 || s.y + px < 0 || s.x - px > W || s.y - px > H) continue;
			// Authored for an additive glow; drawn over the scene instead, they need
			// the extra heat to burn rather than smoulder.
			const Vec4 c = Ocs(Hot(lerp(p.col0, p.col1, u * u) * 1.6f).xyz(), alpha);
			if (p.type == 1)
			{
				r.Ring(p.p, size, std::max(1.5f, px * 0.10f), WithAlpha(c, alpha * 0.8f), 20);
				continue;
			}
			const float stretch = p.stretch * saturatef(length(p.v) / 45.0f);
			if (stretch > 0.12f && px > 1.5f)
			{
				const Vec2 d = normalize(p.v);
				const Vec2 tail(-d.x * size * stretch * 2.6f * r.Scale(), d.y * size * stretch * 2.6f * r.Scale());
				r.ScreenLine(s + tail, s, std::max(1.5f, px * 0.7f), c);
			}
			else if (px <= kBlockPx)
			{
				r.Rect(s.x - px * 0.5f, s.y - px * 0.5f, px, px, c);
			}
			else
			{
				Vec2 oct[8];
				for (int i = 0; i < 8; ++i) oct[i] = s + fromAngle(kTau * (i + 0.5f) / 8.0f) * (px * 0.5f);
				r.ScreenPoly(oct, Fill::Solid(c));
			}
		}

		// Debris: flat-shaded triangles tumbling in 3D, foreshortened as they turn
		// and lit by how squarely they face the camera.
		for (auto& s : shards_)
		{
			float u = 1.0f - s.life / s.maxLife;
			float size = s.size * (1.0f - u * 0.5f);
			static const Vec3 kShard[3] = {{1.0f, 0.0f, 0.0f}, {-0.5f, 0.85f, 0.1f}, {-0.45f, -0.8f, -0.1f}};
			Vec2 tri[3];
			for (int i = 0; i < 3; ++i)
			{
				const Vec3 v = Rotate(s.q, kShard[i] * size);
				tri[i] = s.p + Vec2(v.x, v.y);
			}
			const float facing = fabsf(Rotate(s.q, Vec3(0, 0, 1)).z);
			r.Poly(tri, Fill::Solid(Ocs(Hot(s.col * (0.35f + 0.65f * facing)).xyz(), (1.0f - u) * (1.0f - u))));
		}

		// Bullets. Hue says where a shot came from: yours, theirs, or one the field
		// just sent back the way it came.
		for (auto& b : bullets_)
		{
			if (!r.Visible(b.p, b.p, 2.0f)) continue;
			Vec4 c = Ocs(hsv(b.hue, 0.72f, 1.0f));
			Vec2 dir = normalize(b.v);
			r.Glow(b.p, 1.5f, WithAlpha(c, 0.55f));
			r.Line(b.p - dir * 1.1f, b.p + dir * 0.45f, std::max(2.0f, 0.36f * r.Scale()), Mix(c, kWhite, 0.55f));
		}

		// Engine plume core.
		if (thrustVis_ > 0.02f && mode_ == Mode::Playing)
		{
			Vec2 up = upFromAngle(shipAng_);
			for (int i = 0; i < 4; ++i)
			{
				float f = i / 3.0f;
				Vec2 p = shipPos_ - up * (1.15f + f * 1.9f * thrustVis_);
				float s = (1.05f - f * 0.62f) * thrustVis_;
				Vec3 c = lerp(Vec3(1.3f, 1.9f, 2.7f), Vec3(0.8f, 0.15f, 0.6f), f);
				r.Glow(p, s * 1.6f, Hot(c, 0.9f - f * 0.45f));
			}
		}

		// Shield bubble. Faint enough to ignore until it engages.
		if (shieldVis_ > 0.01f)
		{
			const int seg = 22;
			Vec2 ring[seg];
			float rad = 3.0f + shieldVis_ * 0.5f + sinf(time_ * 9.0f) * 0.12f;
			for (int i = 0; i < seg; ++i)
			{
				float a = kTau * i / seg;
				ring[i] = shipPos_ + fromAngle(a) * (rad * (1.0f + 0.12f * sinf(a * 5.0f + time_ * 7.0f)));
			}
			const float k = saturatef(shieldVis_);
			r.Poly(ring,
			       Fill::Radial(shipPos_, rad * 1.1f, Ocs(0.3f, 0.8f, 1.0f, 0.0f), Ocs(0.35f, 0.85f, 1.0f, 0.30f * k)),
			       Ocs(0.55f, 0.95f, 1.0f, 0.95f * k), 2.0f);
		}

		// Tether: a short chain of links, so the rope drapes and whips instead of
		// behaving like a stick.
		if (podAttached_ && !ropeLinks_.empty())
		{
			Vec2 prev = shipPos_ - upFromAngle(shipAng_) * 1.1f;
			Vec4 col = Mix(Hex(0x66DDFF), Hex(0xFF6688), tetherTension_);
			const float width = (0.32f + tetherTension_ * 0.28f) * r.Scale();
			for (size_t i = 0; i <= ropeLinks_.size(); ++i)
			{
				Vec2 cur = i < ropeLinks_.size() && world_.Valid(ropeLinks_[i]) ? world_[ropeLinks_[i]].pos : podPos_;
				r.Line(prev, cur, width, col);
				r.Block(cur, width * 1.6f, Mix(col, kWhite, 0.4f));
				prev = cur;
			}
			Spark(r, shipPos_ - upFromAngle(shipAng_) * 1.1f, 1.2f, time_ * 3.0f, Mix(col, kWhite, 0.5f));
			Spark(r, podPos_, 2.0f, -time_ * 2.0f, WithAlpha(Mix(col, kWhite, 0.3f), 0.8f));
		}

		// Prop frames. A leg still standing is a promise that the drum has not
		// moved yet.
		for (auto& p : props_)
		{
			if (!p.alive || !world_.Valid(p.body)) continue;
			const Body& pb = world_[p.body];
			const int js[2] = {p.jointA, p.jointB};
			for (int j : js)
			{
				if (j < 0 || j >= world_.JointCount()) continue;
				const Joint& jt = world_.JointAt(j);
				if (!jt.alive) continue;
				float strain = saturatef(jt.load / std::max(jt.breakImpulse, 1.0f));
				Vec4 c = Mix(Hex(0x5577AA), Hex(0xFFAA44), strain);
				r.Line(jt.localB, pb.ToWorld(jt.localA), std::max(2.0f, 0.42f * r.Scale()), c);
				r.Block(jt.localB, std::max(3.0f, 0.6f * r.Scale()), Mix(c, kWhite, 0.3f));
			}
		}

		// Tractor beam searching for the orb or the nearest fuel.
		if (tractor_ && !podAttached_ && mode_ == Mode::Playing)
		{
			Vec2 target = podPos_;
			float best = length(podPos_ - shipPos_);
			for (auto& f : fuel_cells_)
			{
				if (!f.alive) continue;
				float d = length(f.p - shipPos_);
				if (d < best)
				{
					best = d;
					target = f.p;
				}
			}
			if (best < 26.0f)
			{
				Vec2 d = normalize(target - shipPos_);
				Vec2 n = perp(d);
				for (int i = 0; i < 3; ++i)
				{
					float ph = time_ * 5.0f + i * 2.1f;
					float spread = 1.6f * (0.4f + 0.6f * sinf(ph));
					r.Line(shipPos_ + d * 1.5f, target + n * spread, 2.0f, Ocs(0.4f, 0.9f, 1.0f, 0.55f));
				}
				r.Glow(target, 3.0f + sinf(time_ * 8.0f) * 0.6f, Ocs(0.4f, 0.9f, 1.0f, 0.55f));
			}
		}

		// Gravity fields. Motes spiral in on an attractor and out on a repulsor,
		// and the dashed rim marks exactly where the pull starts - the player has
		// to be able to judge the edge from outside it.
		for (auto& w : wells_)
		{
			if (!w.alive || !r.Visible(w.pos, w.pos, w.radius)) continue;
			bool pull = w.strength > 0;
			Vec3 col = pull ? Vec3(0.65f, 0.5f, 1.0f) : Vec3(0.35f, 1.0f, 0.85f);
			const int arms = 5, beads = 9;
			for (int a = 0; a < arms; ++a)
				for (int k = 0; k < beads; ++k)
				{
					float t = fmodf(time_ * 0.42f + k * (1.0f / beads) + a * 0.017f, 1.0f);
					float rr = pull ? 1.0f - t : t; // 0 at the core, 1 at the rim
					float ang = a * (kTau / arms) + rr * 4.2f * (pull ? 1.0f : -1.0f) + time_ * 0.5f;
					Vec2 p = w.pos + fromAngle(ang) * (2.2f + rr * (w.radius - 2.2f));
					float bright = (1.0f - rr) * (1.0f - rr) * 0.8f + 0.2f;
					r.Block(p, std::max(2.0f, (0.25f + (1.0f - rr) * 0.45f) * r.Scale()),
					        Ocs(col * (bright * (1.0f + w.flare)), 0.9f));
				}
			DashedRing(r, w.pos, w.radius, 40, 3, time_ * (pull ? 0.25f : -0.25f), 2.0f, Ocs(col * 0.7f, 0.55f));
		}

		// Mines: a proximity ring that closes as the fuse runs, so the danger zone
		// is a shape on screen and not a number the player has to remember.
		for (auto& m : mines_)
		{
			if (!m.alive || m.fuse <= 0) continue;
			float shrink = saturatef(m.fuse / 1.05f);
			DashedRing(r, m.p, 3.0f + shrink * 9.5f, 18, 2, -m.phase, 2.5f,
			           Ocs(1.0f, 0.45f, 0.15f, 0.55f + 0.45f * (1.0f - shrink)));
		}

		// Drones: an exhaust wisp, and a lock-on tick to the ship while committed.
		for (auto& d : drones_)
		{
			if (!d.alive) continue;
			Vec2 back = d.p - fromAngle(d.ang + kPi * 0.5f) * 1.3f;
			r.Glow(back, 0.5f + d.thrust * 1.1f, Ocs(0.3f + 0.7f * d.thrust, 0.25f * d.thrust, 0.15f, 0.8f));
			if (!d.hunting) continue;
			Vec2 dir = normalize(shipPos_ - d.p);
			float pulse = 0.35f + 0.35f * sinf(time_ * 9.0f);
			r.Line(d.p + dir * 2.2f, d.p + dir * (5.0f + pulse * 4.0f), 2.0f, Ocs(1.0f, 0.3f, 0.2f, pulse + 0.2f));
		}

		// Turret telegraph: the muzzle charges over the last moments before a shot,
		// so being hit is something you saw coming.
		for (auto& t : turrets_)
		{
			if (!t.alive || !t.tracking) continue;
			float charge = saturatef(1.0f - t.cooldown / 0.45f);
			if (charge < 0.02f) continue;
			Vec2 muzzle = t.pos + fromAngle(t.aim) * 3.0f;
			r.Glow(muzzle, 0.6f + charge * 1.8f, Ocs(1.0f, 0.3f, 0.45f, 0.9f * charge));
			Spark(r, muzzle, 0.3f + charge * 1.1f, time_ * 6.0f, Ocs(1.0f, 0.75f, 0.8f, charge));
		}

		// Factory danger halo.
		if (!reactorDead_ && spec_.reactorHits > 0)
		{
			float dmg = (float)reactorHits_ / spec_.reactorHits;
			float rad = 6.0f + dmg * 3.0f + sinf(time_ * (4.0f + dmg * 10.0f)) * 0.6f;
			DashedRing(r, reactorPos_, rad, 26, 3, time_ * 0.6f, 2.5f,
			           Ocs(0.7f + dmg * 0.3f, 0.5f - dmg * 0.25f, 0.15f, 0.55f + dmg * 0.4f));
		}

		// The exit gate across the top of the map.
		if (mode_ != Mode::Title)
		{
			float y = field_.escapeY;
			float x0 = camPos_.x - 90.0f, x1 = camPos_.x + 90.0f;
			const int seg = 22;
			for (int i = 1; i < seg; i += 2)
			{
				float pulse = 0.5f + 0.5f * sinf(time_ * 3.0f + i * 0.6f);
				r.Line(Vec2(lerpf(x0, x1, (float)i / seg), y), Vec2(lerpf(x0, x1, (float)(i + 1) / seg), y),
				       std::max(2.0f, 0.35f * r.Scale()), Ocs(0.25f, 0.6f + 0.4f * pulse, 0.45f, 0.75f));
			}
		}

		// Shockwaves: a hard ring with a soft one behind it, thrown out by
		// anything big enough to go off.
		for (auto& s : shocks_)
		{
			if (s.strength <= 0) continue;
			float k = saturatef(1.0f - s.t / 1.1f);
			float rad = 3.0f + s.t * 30.0f * (0.6f + 0.4f * s.strength);
			r.Ring(s.world, rad, 2.0f + 7.0f * s.strength * k, Ocs(1.0f, 0.85f, 0.6f, 0.75f * k * s.strength), 40);
			r.Ring(s.world, rad * 0.86f, 1.0f + 14.0f * s.strength * k, Ocs(1.0f, 0.5f, 0.2f, 0.25f * k * s.strength),
			       40);
		}
	}

	// --------------------------------------------------------------------- HUD

	// Dark glass with a bevel, lit along its top and left and shadowed along its
	// bottom and right, as a Workbench gadget is.
	static void HudPanel(Renderer& r, Vec2 topLeft, Vec2 size, float alpha = 0.72f)
	{
		r.Rect(topLeft.x, topLeft.y, size.x, size.y, Vec4(0.02f, 0.035f, 0.09f, alpha));
		const Vec4 light = WithAlpha(Hex(0x6688BB), alpha), dark = WithAlpha(Hex(0x000011), alpha);
		r.Rect(topLeft.x, topLeft.y, size.x, 1.0f, light);
		r.Rect(topLeft.x, topLeft.y, 1.0f, size.y, light);
		r.Rect(topLeft.x, topLeft.y + size.y - 1.0f, size.x, 1.0f, dark);
		r.Rect(topLeft.x + size.x - 1.0f, topLeft.y, 1.0f, size.y, dark);
	}

	static void HudFrame(Renderer& r, Vec2 topLeft, Vec2 size, Vec4 col, float w)
	{
		r.Rect(topLeft.x, topLeft.y, size.x, w, col);
		r.Rect(topLeft.x, topLeft.y + size.y - w, size.x, w, col);
		r.Rect(topLeft.x, topLeft.y, w, size.y, col);
		r.Rect(topLeft.x + size.x - w, topLeft.y, w, size.y, col);
	}

	// Glass sized to a block of centred text lines.
	static void CentredPanel(Renderer& r, float cx, float top, float width, float height, float pad,
	                         float alpha = 0.72f)
	{
		HudPanel(r, {cx - width * 0.5f - pad, top - pad}, {width + pad * 2, height + pad * 2}, alpha);
	}

	// A segmented gauge: lit blocks with a bright top, the empty ones dark.
	static void Bar(Renderer& r, Vec2 pos, float w, float h, float frac, Vec4 col)
	{
		HudFrame(r, {pos.x - 2, pos.y - 2}, {w + 4, h + 4}, Scale(col, 0.45f), 1.0f);
		const int blocks = 20;
		const float pitch = w / blocks;
		const float lit = saturatef(frac) * blocks;
		for (int i = 0; i < blocks; ++i)
		{
			const float x = pos.x + i * pitch;
			if (i + 0.5f <= lit)
			{
				r.Rect(x, pos.y, pitch - 2.0f, h, col);
				r.Rect(x, pos.y, pitch - 2.0f, std::max(1.0f, h * 0.25f), Mix(col, kWhite, 0.5f));
			}
			else
			{
				r.Rect(x, pos.y, pitch - 2.0f, h, Scale(col, 0.16f));
			}
		}
	}

	void Game::RenderHud()
	{
		Renderer& r = r_;
		const float W = r.Width(), H = r.Height();
		const float s = std::max(1.0f, H / 900.0f);
		char buf[160];

		// Anything critical pulses, because a bar that is merely short is easy to
		// miss mid-manoeuvre.
		float alarm = 0.55f + 0.45f * sinf(time_ * 9.0f);
		Vec4 fuelCol = fuel_ < 25 ? Scale(kRed, alarm) : kGreen;
		bool locked = shieldLock_ > 0.35f;
		Vec4 shieldCol = locked ? Scale(kRed, alarm) : shield_ < 30 ? kWarm : Hex(0x55AAFF);

		float x = 26 * s, y = 26 * s, bw = 250 * s, bh = 16 * s, gap = 30 * s;
		HudPanel(r, {x - 14 * s, y - 12 * s}, {84 * s + bw + 28 * s, gap * 2 + bh + 24 * s});
		DrawGlyphText(r, "FUEL", {x, y}, 15 * s, fuel_ < 25 ? fuelCol : kCyan, 1.6f * s);
		Bar(r, {x + 84 * s, y}, bw, bh, fuel_ / FUEL_MAX, fuelCol);
		y += gap;
		DrawGlyphText(r, locked ? "DOWN" : "FIELD", {x, y}, 15 * s, locked ? shieldCol : kCyan, 1.6f * s);
		Bar(r, {x + 84 * s, y}, bw, bh, shield_ / SHIELD_MAX, shieldCol);
		y += gap;
		DrawGlyphText(r, "WARP", {x, y}, 15 * s, kCyan, 1.6f * s);
		Bar(r, {x + 84 * s, y}, bw, bh, dilation_ / 100.0f, kViolet);

		// The score is the rubric as it stands, so the player can watch fuel and
		// field damage eat into it while they fly.
		int live = LiveScore();
		sprintf_s(buf, "SCORE %d%%", live);
		float scoreW = GlyphTextWidth(buf, 22 * s);
		char levelBuf[160];
		sprintf_s(levelBuf, "%02d  %s", spec_.index + 1, spec_.name);
		float levelW = GlyphTextWidth(levelBuf, 14 * s);
		char statBuf[160];
		sprintf_s(statBuf, "BEST %d%%   RELICS %d/%d", profile_.At(spec_.index).bestScore, run_.relics,
		          (int)relics_.size());
		float statW = GlyphTextWidth(statBuf, 13 * s);
		float clusterW = std::max(scoreW, std::max(levelW, statW));
		HudPanel(r, {W - clusterW - 40 * s, 14 * s}, {clusterW + 26 * s, 96 * s});
		Vec4 scoreCol = live >= 90 ? kGold : live >= 70 ? Hex(0xDDEEFF) : kWarm;
		DrawGlyphText(r, buf, {W - scoreW - 26 * s, 24 * s}, 22 * s, scoreCol, 1.9f * s);
		DrawGlyphText(r, levelBuf, {W - levelW - 26 * s, 56 * s}, 14 * s, kCyan, 1.3f * s);
		DrawGlyphText(r, statBuf, {W - statW - 26 * s, 78 * s}, 13 * s, kGreen, 1.2f * s);

		// Persistent objective line: the briefing scrolls away, this does not.
		if (mode_ == Mode::Playing)
		{
			const char* goal = podAttached_                               ? "HAUL THE ORB TO THE EXIT"
			                   : !run_.orbReached                         ? "TRACTOR THE ORB"
			                   : (spec_.reactorHits > 0 && !reactorDead_) ? "BREAK THE FACTORY"
			                                                              : "CLIMB OUT THE TOP";
			float gw = GlyphTextWidth(goal, 14 * s);
			CentredPanel(r, W * 0.5f, 26 * s, gw, 14 * s, 12 * s, 0.6f);
			DrawGlyphTextCentred(r, goal, {W * 0.5f, 26 * s}, 14 * s, kGreen, 1.2f * s);
		}

		// Objective markers for anything off screen.
		struct Marker
		{
			Vec2 world;
			Vec4 col;
			const char* label;
			bool show;
		};
		Marker markers[3] = {{podPos_, kViolet, "ORB", !podAttached_},
		                     {reactorPos_, kWarm, "FCTY", spec_.reactorHits > 0 && !reactorDead_},
		                     {Vec2(shipPos_.x, field_.escapeY), kGreen, "EXIT", podAttached_}};
		for (auto& mk : markers)
		{
			if (!mk.show) continue;
			Vec2 at = r.ToScreen(mk.world);
			if (at.x > W * 0.06f && at.x < W * 0.94f && at.y > H * 0.08f && at.y < H * 0.92f) continue;
			Vec2 dir = normalize(mk.world - shipPos_);
			Vec2 c(W * 0.5f, H * 0.5f);
			float rad = std::min(W, H) * 0.36f;
			Vec2 p(c.x + dir.x * rad, c.y - dir.y * rad);
			Vec2 d(dir.x, -dir.y), n = perp(d);
			const Vec2 arrow[3] = {p + d * (12 * s), p - d * (7 * s) + n * (8 * s), p - d * (7 * s) - n * (8 * s)};
			r.ScreenPoly(arrow, Fill::Solid(mk.col), Scale(mk.col, 0.3f), 1.5f);
			DrawGlyphTextCentred(r, mk.label, {p.x, p.y + 12 * s}, 11 * s, mk.col, 1.0f * s, 0.70f, true);
		}

		// Radar.
		{
			float rw = 250 * s, rh = 150 * s;
			float rx = W - rw - 26 * s, ry = H - rh - 26 * s;
			HudPanel(r, {rx - 12 * s, ry - 12 * s}, {rw + 24 * s, rh + 24 * s});
			HudFrame(r, {rx, ry}, {rw, rh}, Ocs(0.2f, 0.45f, 0.65f, 0.6f), 1.0f);

			Vec2 lo = field_.Min(), hi = field_.Max();
			auto toRadar = [&](Vec2 w)
			{
				return Vec2(rx + (w.x - lo.x) / (hi.x - lo.x) * rw, ry + (1.0f - (w.y - lo.y) / (hi.y - lo.y)) * rh);
			};
			for (size_t i = 1; i < field_.path.size(); ++i)
				r.ScreenLine(toRadar(field_.path[i - 1]), toRadar(field_.path[i]), 2.0f * s,
				             Ocs(0.12f, 0.3f, 0.45f, 0.8f));
			auto dot = [&](Vec2 w, Vec4 c, float sz)
			{
				Vec2 p = toRadar(w);
				r.Rect(p.x - sz * s * 0.5f, p.y - sz * s * 0.5f, sz * s, sz * s, c);
			};
			for (auto& t : turrets_)
				if (t.alive) dot(t.pos, Ocs(1.0f, 0.3f, 0.35f), 3.0f);
			for (auto& w : wells_)
			{
				if (!w.alive) continue;
				Vec4 c = w.strength > 0 ? Ocs(0.7f, 0.55f, 1.0f) : Ocs(0.35f, 1.0f, 0.85f);
				Vec2 a = toRadar(w.pos);
				float rr = w.radius / (hi.x - lo.x) * rw;
				const int seg = 16;
				for (int i = 1; i < seg; i += 2)
				{
					float a0 = kTau * i / seg, a1 = kTau * (i + 1) / seg;
					r.ScreenLine(a + fromAngle(a0) * rr, a + fromAngle(a1) * rr, 1.0f, WithAlpha(c, 0.5f));
				}
				dot(w.pos, c, 4.0f);
			}
			for (auto& m : mines_)
				if (m.alive)
					dot(m.p, m.fuse > 0 ? Ocs(1.0f, 0.9f, 0.7f) : Ocs(0.9f, 0.55f, 0.15f), m.fuse > 0 ? 4.0f : 3.0f);
			for (auto& d : drones_)
				if (d.alive) dot(d.p, Ocs(1.0f, 0.35f, 0.3f), d.hunting ? 5.0f : 3.0f);
			for (auto& f : fuel_cells_)
				if (f.alive) dot(f.p, Ocs(0.3f, 0.95f, 0.55f), 3.0f);
			for (auto& rl : relics_)
				if (!rl.taken) dot(rl.p, kGold, 4.0f);
			for (auto& p : props_)
				if (p.alive && p.kind == PROP_TANK && world_.Valid(p.body))
					dot(world_[p.body].pos, Ocs(1.0f, 0.6f, 0.15f), 3.0f);
			if (!reactorDead_ && spec_.reactorHits > 0) dot(reactorPos_, kWarm, 6.0f);
			dot(podPos_, kViolet, 5.0f);
			dot(shipPos_, kWhite, 6.0f);
			Vec2 ey = toRadar({lo.x, field_.escapeY});
			r.Rect(rx, ey.y, rw, 1.0f, Ocs(0.2f, 0.8f, 0.45f, 0.6f));
		}

		if (msgTime_ > 0 && !msg_.empty())
		{
			float a = saturatef(msgTime_ * 2.0f);
			float mw = GlyphTextWidth(msg_.c_str(), 18 * s);
			CentredPanel(r, W * 0.5f, H - 130 * s, mw, 18 * s, 16 * s, 0.72f * a);
			DrawGlyphTextCentred(r, msg_.c_str(), {W * 0.5f, H - 130 * s}, 18 * s, WithAlpha(Hex(0xBBEEFF), a),
			                     1.5f * s);
		}

		// The manual replaces the centre cards rather than being read through them.
		if (mode_ == Mode::Briefing && !showHelp_)
		{
			sprintf_s(buf, "GRAVITY %.1f   TURRETS %d   DRUMS %d", spec_.gravity, (int)turrets_.size(),
			          spec_.tankCount);
			CentredPanel(r, W * 0.5f, H * 0.30f, GlyphTextWidth(buf, 13 * s) * 1.15f, 210 * s, 30 * s, 0.78f);
			DrawGlyphTextCentred(r, buf, {W * 0.5f, H * 0.30f + 100 * s}, 13 * s, kCyan, 1.1f * s);
			if (!mines_.empty() || !drones_.empty() || !wells_.empty())
			{
				sprintf_s(buf, "MINES %d   DRONES %d   GRAVITY WELLS %d", (int)mines_.size(), (int)drones_.size(),
				          (int)wells_.size());
				DrawGlyphTextCentred(r, buf, {W * 0.5f, H * 0.30f + 122 * s}, 13 * s, kWarm, 1.1f * s);
			}
			DrawChromeTextCentred(r, spec_.name, {W * 0.5f, H * 0.30f}, 44 * s, 5.0f * s);
			DrawGlyphTextCentred(r, spec_.archetypeName, {W * 0.5f, H * 0.30f + 60 * s}, 20 * s, kWarm, 1.6f * s);
			DrawGlyphTextCentred(r, "ONE SHIP - THE FIELD IS YOUR ONLY MARGIN", {W * 0.5f, H * 0.30f + 150 * s}, 15 * s,
			                     kGreen, 1.3f * s);
			DrawGlyphTextCentred(r, "ENTER TO LAUNCH", {W * 0.5f, H * 0.30f + 174 * s}, 16 * s, kCyan, 1.4f * s);
		}

		// One hazard is introduced per level; this is where it gets named.
		if (!newThreat_.empty() && !showHelp_ && (mode_ == Mode::Briefing || modeTime_ < 7.0f))
		{
			float a = mode_ == Mode::Briefing ? 1.0f : saturatef(7.0f - modeTime_);
			float nw = GlyphTextWidth(newThreat_.c_str(), 15 * s);
			CentredPanel(r, W * 0.5f, H - 176 * s, nw, 15 * s, 14 * s, 0.75f * a);
			DrawGlyphTextCentred(r, newThreat_.c_str(), {W * 0.5f, H - 176 * s}, 15 * s, WithAlpha(kWarm, a), 1.4f * s);
		}

		if (paused_ && !showHelp_)
		{
			CentredPanel(r, W * 0.5f, H * 0.4f, GlyphTextWidth("P TO RESUME   ESC FOR THE STAR MAP", 14 * s), 102 * s,
			             30 * s, 0.8f);
			DrawChromeTextCentred(r, "PAUSED", {W * 0.5f, H * 0.4f}, 46 * s, 5.5f * s);
			DrawGlyphTextCentred(r, "P TO RESUME   ESC FOR THE STAR MAP", {W * 0.5f, H * 0.4f + 64 * s}, 14 * s, kCyan,
			                     1.2f * s);
		}

		if (slowMo_) DrawGlyphTextCentred(r, "WARP", {W * 0.5f, H - 60 * s}, 14 * s, kViolet, 1.2f * s, 0.70f, true);

		if (showHelp_)
		{
			const char* lines[] = {"ZPILOT  FLIGHT MANUAL",
			                       "",
			                       "LEFT / RIGHT ... ROTATE",
			                       "UP ............ MAIN THRUST",
			                       "DOWN .......... RETRO THRUST",
			                       "SPACE ......... CANNON",
			                       "SHIFT ......... TETHER / RELEASE THE ORB",
			                       "CTRL .......... OVERCHARGE THE FIELD",
			                       "TAB ........... WARP - SLOWS EVERYTHING",
			                       "",
			                       "ONE SHIP PER PLANET. THE FIELD COMES BACK BY",
			                       "ITSELF, PAID FOR OUT OF YOUR FUEL. IT PUSHES YOU",
			                       "OFF WALLS AND BOUNCES SHOTS BACK THE WAY THEY",
			                       "CAME. A WALL TAKES A BITE AND KNOCKS IT OFFLINE.",
			                       "YOU ONLY DIE WITH THE FIELD ALREADY FLAT.",
			                       "",
			                       "SHOOT A DRUM TO CLEAR A NEST, BLOW A HOLE, OR",
			                       "LAUNCH YOURSELF. THEY SET EACH OTHER OFF.",
			                       "THE ORB SWINGS ON ITS ROPE - USE IT. YOU ONLY",
			                       "FINISH A PLANET BY CARRYING IT OUT THE TOP.",
			                       "",
			                       "THREE RELICS ARE HIDDEN OFF THE FAST LINE.",
			                       "THE SCORE IS FUEL LEFT, FIELD SPARED AND RELICS.",
			                       "",
			                       "F1 HELP   F3 MUTE   F11 FULLSCREEN",
			                       "P PAUSE   R RETRY   ESC STAR MAP"};
			const int n = (int)_countof(lines);
			const float ls = 15 * s, lh = 27 * s, pad = 34 * s;
			float wide = 0;
			for (auto& l : lines) wide = std::max(wide, GlyphTextWidth(l, ls));
			float blockH = (n - 1) * lh + ls;
			float x0 = W * 0.5f - wide * 0.5f, y0 = H * 0.5f - blockH * 0.5f;

			HudPanel(r, {x0 - pad, y0 - pad}, {wide + pad * 2, blockH + pad * 2}, 0.88f);
			HudFrame(r, {x0 - pad * 0.6f, y0 - pad * 0.6f}, {wide + pad * 1.2f, blockH + pad * 1.2f},
			         Ocs(0.25f, 0.6f, 0.85f, 0.8f), std::max(1.0f, 1.0f * s));
			for (int i = 0; i < n; ++i)
				DrawGlyphText(r, lines[i], {x0, y0 + i * lh}, ls, i == 0 ? kWarm : Hex(0x99CCFF), 1.2f * s);
		}
	}

	void Game::RenderTitle()
	{
		Renderer& r = r_;
		const float W = r.Width(), H = r.Height();
		const float s = std::max(1.0f, H / 900.0f);

		// Raster bars sweeping behind the logo: each one a copper ramp, a palette
		// colour per line, up to a bright centre line and back down.
		const float logoTop = H * 0.16f;
		for (int i = 0; i < 3; ++i)
		{
			const float cy = logoTop + 46 * s + sinf(time_ * 1.3f + i * 2.1f) * 62 * s;
			const Vec3 hot = hsv(0.55f + i * 0.13f, 0.75f, 1.0f);
			const Vec3 cool = hsv(0.55f + i * 0.13f, 0.9f, 0.25f);
			for (int line = -kBarLines; line <= kBarLines; ++line)
			{
				const float k = 1.0f - fabsf((float)line) / kBarLines;
				r.Rect(0, cy + (line - 0.5f) * s, W, s, Ocs(lerp(cool, hot, k * k)));
			}
		}

		float wob = sinf(time_ * 1.5f) * 3.0f * s;
		DrawChromeTextCentred(r, "ZPILOT", {W * 0.5f, logoTop + wob}, 92 * s, 11.0f * s);
		DrawGlyphTextCentred(r, "THIRTY FIVE PLANETS - ONE SHIP EACH", {W * 0.5f, logoTop + 118 * s}, 16 * s, kWarm,
		                     1.4f * s, 0.70f, true);

		float a = 0.55f + 0.45f * sinf(time_ * 3.0f);
		DrawGlyphTextCentred(r, "PRESS ENTER FOR THE STAR MAP", {W * 0.5f, H * 0.62f}, 24 * s,
		                     Mix(Hex(0x337755), Hex(0x88FFCC), a), 1.8f * s, 0.70f, true);

		const char* lines[] = {"ROTATE  LEFT / RIGHT      THRUST  UP      RETRO  DOWN",
		                       "FIRE  SPACE      TETHER  SHIFT      OVERCHARGE  CTRL",
		                       "WARP  TAB      F1 MANUAL      R RETRY"};
		float y = H * 0.74f;
		float wide = 0;
		for (auto& l : lines) wide = std::max(wide, GlyphTextWidth(l, 14 * s));
		CentredPanel(r, W * 0.5f, y, wide, 74 * s, 26 * s, 0.7f);
		for (auto& l : lines)
		{
			DrawGlyphTextCentred(r, l, {W * 0.5f, y}, 14 * s, Hex(0x88CCEE), 1.1f * s);
			y += 30 * s;
		}
		DrawGlyphTextCentred(r, "AFTER THRUST 1986 BY JEREMY C SMITH", {W * 0.5f, H - 54 * s}, 12 * s, Hex(0x7799BB),
		                     1.0f * s, 0.70f, true);
	}

	// ------------------------------------------------------------- star map

	static Vec4 MedalColour(int medal)
	{
		switch (medal)
		{
		case MEDAL_GOLD: return Hex(0xFFDD55);
		case MEDAL_SILVER: return Hex(0xDDEEFF);
		case MEDAL_BRONZE: return Hex(0xEE9955);
		default: return Hex(0x6688AA);
		}
	}

	void Game::RenderGrid()
	{
		Renderer& r = r_;
		const float W = r.Width(), H = r.Height();
		const float s = std::max(1.0f, H / 900.0f);
		char buf[160];

		DrawChromeTextCentred(r, "STAR MAP", {W * 0.5f, 22 * s}, 34 * s, 4.5f * s);
		sprintf_s(buf, "COMPLETION %d%%   GOLD %d/%d   SILVER+ %d/%d", profile_.TotalPercent() / kPlanetCount,
		          profile_.MedalCount(MEDAL_GOLD), kPlanetCount, profile_.MedalCount(MEDAL_SILVER), kPlanetCount);
		DrawGlyphTextCentred(r, buf, {W * 0.5f, 68 * s}, 14 * s, kWarm, 1.2f * s);

		const float pad = 13 * s;
		const float cardW = (W * 0.88f) / kPlanetCols - pad;
		const float cardH = cardW * 0.66f;
		const float x0 = (W - (cardW + pad) * kPlanetCols + pad) * 0.5f;
		const float y0 = H * 0.135f;

		for (int i = 0; i < kPlanetCount; ++i)
		{
			const int col = i % kPlanetCols, row = i / kPlanetCols;
			const float cx = x0 + col * (cardW + pad);
			const float cy = y0 + row * (cardH + pad);
			const bool sel = i == gridSel_;
			const bool open = profile_.Unlocked(i);
			const PlanetRecord& rec = profile_.At(i);

			HudPanel(r, {cx, cy}, {cardW, cardH}, open ? 0.86f : 0.66f);

			// The card shows the real cavern: same seed, same carving, coarser. A run
			// of one tone along a row is one block, and a block the row below repeats
			// exactly grows down to take it in.
			{
				const uint8_t* t = &thumbs_[(size_t)i * kThumbW * kThumbH];
				const float tw = (cardW - 14 * s) / kThumbW;
				const float th = (cardH - 34 * s) / kThumbH;
				const float tx = cx + 7 * s, ty = cy + 6 * s;
				const float dim = open ? 1.0f : 0.32f;
				auto tone = [](uint8_t v)
				{
					return v < 90 ? 0 : v < 200 ? 1 : 3;
				};
				auto colour = [&](int level)
				{
					if (level == 0) return Ocs(Vec3(0.10f, 0.13f, 0.20f) * dim, 0.9f);
					const float k = 0.55f + level * 0.15f;
					return rec.completed ? Ocs(Vec3(0.35f, 1.0f, 0.75f) * (k * dim))
					                     : Ocs(Vec3(0.30f, 0.62f, 1.0f) * (k * dim));
				};
				struct Block
				{
					int x0, x1, level, y0;
				};
				std::vector<Block> live, next;
				auto flush = [&](const Block& b, int y1)
				{
					r.Rect(tx + b.x0 * tw, ty + b.y0 * th, (b.x1 - b.x0) * tw, (y1 - b.y0) * th, colour(b.level));
				};
				for (int py = 0; py <= kThumbH; ++py)
				{
					next.clear();
					int start = 0;
					for (int px = 1; py < kThumbH && px <= kThumbW; ++px)
					{
						const int level = tone(t[(size_t)py * kThumbW + start]);
						if (px < kThumbW && tone(t[(size_t)py * kThumbW + px]) == level) continue;
						Block b{start, px, level, py};
						for (const Block& old : live)
							if (old.x0 == b.x0 && old.x1 == b.x1 && old.level == b.level) b.y0 = old.y0;
						next.push_back(b);
						start = px;
					}
					for (const Block& old : live)
					{
						bool carried = false;
						for (const Block& b : next) carried |= b.y0 == old.y0 && b.x0 == old.x0 && b.x1 == old.x1;
						if (!carried) flush(old, py);
					}
					live.swap(next);
				}
			}
			if (!open)
				DrawGlyphTextCentred(r, "LOCKED", {cx + cardW * 0.5f, cy + cardH * 0.38f}, 14 * s, Hex(0xCCDDFF),
				                     1.3f * s);

			// A locked planet keeps its name: the number is all the card gives away.
			if (open) sprintf_s(buf, "%02d %s", i + 1, specs_[(size_t)i].name);
			else sprintf_s(buf, "%02d", i + 1);
			DrawGlyphTextCentred(r, buf, {cx + cardW * 0.5f, cy + cardH - 26 * s}, 12 * s, open ? kCyan : Hex(0x4D5D80),
			                     1.0f * s);

			if (open && rec.attempted)
			{
				sprintf_s(buf, "%d%%", rec.bestScore);
				DrawGlyphTextCentred(r, buf, {cx + cardW * 0.5f, cy + cardH - 13 * s}, 11 * s, MedalColour(rec.medal),
				                     1.0f * s);
				// Relic pips: the reason to fly a cleared planet again.
				for (int k = 0; k < 3; ++k)
				{
					const float px = cx + cardW - 12 * s - k * 9 * s;
					r.Rect(px - 3 * s, cy + 6 * s, 6 * s, 6 * s, k < rec.relics ? kGold : Hex(0x334455));
				}
			}

			if (sel)
			{
				// The chosen card's frame cycles through the palette, the way a
				// 16-bit menu marked its cursor.
				const Vec4 cycle = Ocs(hsv(time_ * 0.45f, 0.55f, 1.0f));
				HudFrame(r, {cx - 2 * s, cy - 2 * s}, {cardW + 4 * s, cardH + 4 * s}, cycle, std::max(2.0f, 2.5f * s));
				HudFrame(r, {cx - 6 * s, cy - 6 * s}, {cardW + 12 * s, cardH + 12 * s}, WithAlpha(cycle, 0.45f),
				         std::max(1.0f, 1.0f * s));
			}
			else
			{
				HudFrame(r, {cx, cy}, {cardW, cardH},
				         open ? Ocs(0.2f, 0.4f, 0.62f, 0.8f) : Ocs(0.14f, 0.18f, 0.26f, 0.8f), 1.0f);
			}
		}

		// Detail strip for the highlighted planet.
		const LevelSpec& sel = specs_[(size_t)gridSel_];
		const PlanetRecord& rec = profile_.At(gridSel_);
		float dy = y0 + kPlanetRows * (cardH + pad) + 10 * s;
		if (profile_.Unlocked(gridSel_))
		{
			sprintf_s(buf, "%s   %s   SYSTEM %d", sel.name, sel.archetypeName, sel.row + 1);
			DrawGlyphTextCentred(r, buf, {W * 0.5f, dy}, 18 * s, kCyan, 1.6f * s);
			if (rec.attempted)
			{
				sprintf_s(buf, "BEST %d%% %s   RELICS %d/3   ATTEMPTS %u", rec.bestScore, MedalName(rec.medal),
				          rec.relics, rec.attempts);
				DrawGlyphTextCentred(r, buf, {W * 0.5f, dy + 26 * s}, 13 * s, MedalColour(rec.medal), 1.1f * s);
			}
			else
			{
				DrawGlyphTextCentred(r, "UNFLOWN", {W * 0.5f, dy + 26 * s}, 13 * s, kWarm, 1.1f * s);
			}
			DrawGlyphTextCentred(r, "ENTER TO LAUNCH      ARROWS TO CHOOSE      ESC TO TITLE", {W * 0.5f, dy + 50 * s},
			                     13 * s, kGreen, 1.1f * s);
		}
		else
		{
			DrawGlyphTextCentred(r, "SYSTEM LOCKED - CLEAR FOUR PLANETS IN THE SYSTEM ABOVE", {W * 0.5f, dy + 12 * s},
			                     16 * s, kWarm, 1.4f * s);
		}
	}

	// -------------------------------------------------------------- results

	void Game::RenderResults()
	{
		Renderer& r = r_;
		const float W = r.Width(), H = r.Height();
		const float s = std::max(1.0f, H / 900.0f);
		char buf[160];

		r.Rect(0, 0, W, H, Vec4(0.01f, 0.02f, 0.05f, 0.78f));

		float top = H * 0.16f;
		const char* headline = run_.completed ? "ORBIT ACHIEVED" : run_.lost ? "SHIP LOST" : "MISSION ABANDONED";
		Vec4 headCol = run_.completed ? kGreen : (run_.lost ? kRed : kWarm);
		DrawGlyphTextCentred(r, headline, {W * 0.5f, top}, 42 * s, headCol, 2.6f * s);
		DrawGlyphTextCentred(r, run_.completed ? spec_.name : failReason_, {W * 0.5f, top + 52 * s}, 20 * s, kWarm,
		                     1.6f * s);

		sprintf_s(buf, "%d%%", run_.percent);
		DrawChromeTextCentred(r, buf, {W * 0.5f, top + 92 * s}, 64 * s, 8.0f * s);
		if (run_.medal > MEDAL_NONE)
			DrawGlyphTextCentred(r, MedalName(run_.medal), {W * 0.5f, top + 172 * s}, 22 * s, MedalColour(run_.medal),
			                     1.8f * s);

		// The breakdown, with the weakest line called out. "SCORE 6240" is not a
		// reason to try again; "FUEL LEFT 4 / 20" is.
		struct Line
		{
			const char* label;
			int got, max;
		};
		Line lines[] = {{"OBJECTIVES", run_.objectiveScore, 50},
		                {"FUEL LEFT", run_.fuelScore, 20},
		                {"FIELD INTEGRITY", run_.integrityScore, 20},
		                {"RELICS", run_.relicScore, 10}};
		int worst = 0;
		for (int i = 1; i < 4; ++i)
		{
			float a = (float)lines[i].got / lines[i].max;
			float b = (float)lines[worst].got / lines[worst].max;
			if (a < b) worst = i;
		}

		float ly = top + 214 * s;
		for (int i = 0; i < 4; ++i)
		{
			bool weak = i == worst && run_.completed;
			sprintf_s(buf, "%-16s %2d / %2d", lines[i].label, lines[i].got, lines[i].max);
			DrawGlyphTextCentred(r, buf, {W * 0.5f, ly}, 16 * s, weak ? kWarm : kCyan, 1.3f * s);
			ly += 26 * s;
		}

		sprintf_s(buf, "FUEL %.0f%%   FIELD SPENT %.0f   RELICS %d/3", run_.fuelLeft, run_.shieldSpent, run_.relics);
		DrawGlyphTextCentred(r, buf, {W * 0.5f, ly + 8 * s}, 13 * s, kGreen, 1.1f * s);

		if (run_.best) DrawGlyphTextCentred(r, "NEW PERSONAL BEST", {W * 0.5f, ly + 34 * s}, 18 * s, kGold, 1.5f * s);

		float a = 0.6f + 0.4f * sinf(time_ * 4.0f);
		DrawGlyphTextCentred(r, "R TO FLY IT AGAIN      ENTER FOR THE STAR MAP", {W * 0.5f, H - 70 * s}, 16 * s,
		                     Mix(Hex(0x337755), Hex(0x88FFCC), a), 1.4f * s);
	}

} // namespace zp
