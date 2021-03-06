#include <math.h>
#include <stdio.h>

#include "libasserv_priv.h"
#include "libasserv_default.h"

#define PI 3.141592653589793

// Mode de génération des rampes
#define M_OFF       0 // asserv off
#define M_FIX       1 // pas de rampe, stay on
#define M_END       2 // pas de rampe, switch off quand err < epsilon
#define M_POS       3 // rampe de position
#define M_SPEED     4 // rampe de vitesse

// XXX debug
#define sendorders SendMotionOrders(deltaOrder.x, deltaOrder.v, deltaOrder.a, alphaOrder.x, alphaOrder.v, alphaOrder.a)

// état des systèmes (position, vitesse)
// utilisé par les pid pour connaître l’erreur
// seul la position ou la vitesse ne sert, suivant le type d’asserv
static volatile State deltaState;
static volatile State alphaState;

// commandes calculé à chaque itération par les rampes,
// fixé au début de la consigne si pas de rampe
static volatile Order deltaOrder;
static volatile Order alphaOrder;

// consignes finales, seulement pour utilisation avec les rampes
static volatile Order deltaFinalOrder;
static volatile Order alphaFinalOrder;

// asserv
static volatile Asserv deltaAsserv;
static volatile Asserv alphaAsserv;

// mode de génération des rampes (ou pas)
static volatile int deltaMode = M_OFF;
static volatile int alphaMode = M_OFF;

// seuils de détection d’atteinte de la consigne
static volatile float epsDist = DEFAULT_EPSILON_DIST;
static volatile float epsSpeed = DEFAULT_EPSILON_SPEED;
static volatile float epsTheta = DEFAULT_EPSILON_THETA;
static volatile float epsOmega = DEFAULT_EPSILON_OMEGA;

// vitesse, accélération et décélération et maximum
static volatile float vDistMax, aDistMax, dDistMax;
static volatile float vRotMax, aRotMax, dRotMax;
static volatile float vDistMaxDefault = DEFAULT_DIST_SPEED_MAX;
static volatile float aDistMaxDefault = DEFAULT_DIST_ACC_MAX;
static volatile float dDistMaxDefault = DEFAULT_DIST_DEC_MAX;
static volatile float vRotMaxDefault = DEFAULT_ROT_SPEED_MAX;
static volatile float aRotMaxDefault = DEFAULT_ROT_ACC_MAX;
static volatile float dRotMaxDefault = DEFAULT_ROT_DEC_MAX;

static void(*done)(void); // callback

void motion_init(void(*_done)(void)) {
    odo_init();
    done = _done;
    asserv_init(&deltaAsserv,
            (PidCoefs)DEFAULT_DELTA_POS_COEFS,
            (PidCoefs)DEFAULT_DELTA_SPEED_COEFS,
            &deltaOrder);
    asserv_init(&alphaAsserv,
            (PidCoefs)DEFAULT_ALPHA_POS_COEFS,
            (PidCoefs)DEFAULT_ALPHA_SPEED_COEFS,
            &alphaOrder);
    deltaOrder.v = 0;
    alphaOrder.v = 0;
}

// XXX debug
int lastDeltaMode = -1;
int lastAlphaMode = -1;

static volatile float cmdDelta, cmdAlpha;

void motion_step(float period, int ticsLeft, int ticsRight, int *cmdLeft, int *cmdRight) {
    int ret;
    float delta, alpha;

    odo_step(ticsLeft, ticsRight, &delta, &alpha);

    deltaState.x += delta;
    deltaState.v = delta / period;
    alphaState.x += alpha;
    alphaState.v = alpha / period;

    // debug
    if (deltaMode != lastDeltaMode || alphaMode != lastAlphaMode) {
        lastDeltaMode = deltaMode;
        lastAlphaMode = alphaMode;
        SendMode(deltaMode, alphaMode);
    }

    switch (deltaMode) {
        case M_POS:
            ret = ramp_dist(period,
                    &(deltaOrder.x), &(deltaOrder.v), &(deltaOrder.a),
                    deltaFinalOrder.x, deltaFinalOrder.v, vDistMax, aDistMax);
            // la vitesse final n’est pas forcément atteinte
            // il peut être judicieux de continuer lorque la vitesse actuelle
            // est supérieur à la vitesse final, ceci impliquand un ineductable
            // dépassement, mais la rampe est capable de faire reculer le robot
            // pour le positionner correctement, avec la bonne vitesse !
            // bref, à améliorer éventuellement.
            if (ret) {
                if (alphaMode == M_POS || alphaMode == M_SPEED) {
                    deltaMode = M_FIX;
                } else {
		    deltaMode = M_END;
                    if (alphaMode == M_FIX) {
                        alphaMode = M_END;
                    }
                }
            }
            break;
        case M_SPEED:
            ramp_speed(period,
                    &(deltaOrder.x), &(deltaOrder.v),
                    deltaFinalOrder.v,
                    aDistMax, vDistMax, dDistMax);
            break;
    }
    switch (alphaMode) {
        case M_POS:
            ret = ramp_dist(period,
                    &(alphaOrder.x), &(alphaOrder.v), &(alphaOrder.a),
                    alphaFinalOrder.x, alphaFinalOrder.v, vRotMax, aRotMax);
            // même remarque
            if (ret) {
                if (deltaMode == M_POS || deltaMode == M_SPEED) {
                    alphaMode = M_FIX;
                } else {
                    alphaMode = M_END;
                    if (deltaMode == M_FIX) {
                        deltaMode = M_END;
                    }
                }
            }
            break;
        case M_SPEED:
            ramp_speed(period,
                    &(alphaOrder.x), &(alphaOrder.v),
                    alphaFinalOrder.v,
                    aRotMax, vRotMax, dRotMax);
            break;
    }

    cmdDelta = asserv_step(&deltaAsserv, period, deltaState);
    cmdAlpha = asserv_step(&alphaAsserv, period, alphaState);

    if (deltaMode == M_END) {
        if (asserv_done(&(deltaAsserv), epsDist, epsSpeed)) {
            deltaMode = M_OFF;
            asserv_off(&(deltaAsserv));
            if (alphaMode == M_FIX) alphaMode = M_END;
            else if (alphaMode == M_OFF) {
                sendorders;
                done();
            }
        }
    }
    if (alphaMode == M_END) {
        if (asserv_done(&(alphaAsserv), epsDist, epsSpeed)) {
            alphaMode = M_OFF;
            asserv_off(&(alphaAsserv));
            if (deltaMode == M_FIX) deltaMode = M_END;
            else if (deltaMode == M_OFF) {
                sendorders;
                done();
            }
        }
    }

    *cmdLeft = (int)(cmdDelta - 2 * cmdAlpha);
    *cmdRight = (int)(cmdDelta + 2 * cmdAlpha);
}

void motion_dist(float dist, float v, float a) {
    // delta
    vDistMax = (v>0)?v:vDistMaxDefault;
    aDistMax = (a>0)?a:aDistMaxDefault;
    deltaMode = M_POS;
    deltaState.x = 0;
    deltaOrder.x = 0;
    deltaFinalOrder.x = dist;
    deltaFinalOrder.v = 0;
    asserv_set_pos_mode(&deltaAsserv);
    // alpha
    alphaMode = M_FIX;
    alphaState.x = 0;
    alphaOrder.x = 0;
    alphaOrder.v = 0;
    asserv_set_pos_mode(&alphaAsserv);
}

void motion_dist_free(float dist) {
    // delta
    deltaMode = M_END;
    deltaState.x = 0;
    deltaOrder.x = dist;
    deltaOrder.v = 0;
    asserv_set_pos_mode(&deltaAsserv);
    // alpha
    alphaMode = M_FIX;
    alphaState.x = 0;
    alphaOrder.x = 0;
    alphaOrder.v = 0;
    asserv_set_pos_mode(&alphaAsserv);
}

void motion_rot(float rot, float v, float a) {
    // delta
    deltaMode = M_FIX;
    deltaState.x = 0;
    deltaOrder.x = 0;
    deltaOrder.v = 0;
    asserv_set_pos_mode(&deltaAsserv);
    // alpha
    vRotMax = (v>0)?v:vRotMaxDefault;
    aRotMax = (a>0)?a:aRotMaxDefault;
    alphaMode = M_POS;
    alphaState.x = 0;
    alphaOrder.x = 0;
    alphaFinalOrder.x = rot;
    alphaFinalOrder.v = 0;
    asserv_set_pos_mode(&alphaAsserv);
}

void motion_rot_free(float rot) {
    // delta
    deltaMode = M_FIX;
    deltaState.x = 0;
    deltaOrder.x = 0;
    deltaOrder.v = 0;
    asserv_set_pos_mode(&deltaAsserv);
    // alpha
    alphaMode = M_END;
    alphaState.x = 0;
    alphaOrder.x = rot;
    alphaOrder.v = 0;
    asserv_set_pos_mode(&alphaAsserv);
}

void motion_dist_rot(float dist, float rot, float vDist, float aDist, float vRot, float aRot) {
    // delta
    vDistMax = (vDist>0)?vDist:vDistMaxDefault;
    aDistMax = (aDist>0)?aDist:aDistMaxDefault;
    deltaMode = M_POS;
    deltaState.x = 0;
    deltaOrder.x = 0;
    deltaFinalOrder.x = dist;
    deltaFinalOrder.v = 0;
    asserv_set_pos_mode(&deltaAsserv);
    // alpha
    vRotMax = (vRot>0)?vRot:vRotMaxDefault;
    aRotMax = (aRot>0)?aRot:aRotMaxDefault;
    alphaMode = M_POS;
    alphaState.x = 0;
    alphaOrder.x = 0;
    alphaFinalOrder.x = rot;
    alphaFinalOrder.v = 0;
    asserv_set_pos_mode(&alphaAsserv);
}

void motion_speed(float v, float a, float d) {
    // delta
    aDistMax = (a>0)?a:aDistMaxDefault;
    dDistMax = (d>0)?d:dDistMaxDefault;
    vDistMax = (v<0)?-v:v;
    deltaMode = M_SPEED;
    deltaFinalOrder.v = v;
    asserv_set_speed_mode(&deltaAsserv);
    // alpha
    alphaMode = M_FIX;
    alphaState.x = 0;
    alphaOrder.x = 0;
    alphaOrder.v = 0;
    asserv_set_pos_mode(&alphaAsserv);
}

void motion_speed_free(float speed) {
    // delta
    deltaMode = M_FIX;
    deltaOrder.v = speed;
    asserv_set_speed_mode(&deltaAsserv);
    // alpha
    alphaMode = M_FIX;
    alphaState.x = 0;
    alphaOrder.x = 0;
    alphaOrder.v = 0;
    asserv_set_pos_mode(&alphaAsserv);
}

void motion_omega(float omega, float a, float d) {
    // delta
    deltaMode = M_FIX;
    deltaState.x = 0;
    deltaOrder.x = 0;
    deltaOrder.v = 0;
    asserv_set_pos_mode(&deltaAsserv);
    // alpha
    aRotMax = (a>0)?a:aRotMaxDefault;
    dRotMax = (d>0)?d:dRotMaxDefault;
    vRotMax = (omega<0)?-omega:omega;
    alphaMode = M_SPEED;
    alphaFinalOrder.v = omega;
    asserv_set_speed_mode(&alphaAsserv);
}

void motion_omega_free(float omega) {
    // delta
    deltaMode = M_FIX;
    deltaState.x = 0;
    deltaOrder.x = 0;
    deltaOrder.v = 0;
    asserv_set_pos_mode(&deltaAsserv);
    // alpha
    alphaMode = M_FIX;
    alphaOrder.v = omega;
    asserv_set_speed_mode(&alphaAsserv);
}

void motion_speed_omega(float speed, float omega, float aDist, float dDist, float aRot, float dRot) {
    // delta
    aDistMax = (aDist>0)?aDist:aDistMaxDefault;
    dDistMax = (dDist>0)?dDist:dDistMaxDefault;
    vDistMax = (speed<0)?-speed:speed;
    deltaMode = M_SPEED;
    deltaFinalOrder.v = speed;
    asserv_set_speed_mode(&deltaAsserv);
    // alpha
    aRotMax = (aRot>0)?aRot:aRotMaxDefault;
    dRotMax = (dRot>0)?dRot:dRotMaxDefault;
    vRotMax = (omega<0)?-omega:omega;
    alphaMode = M_SPEED;
    alphaFinalOrder.v = omega;
    asserv_set_speed_mode(&alphaAsserv);
}

void motion_stop() {
    deltaMode = M_OFF;
    deltaOrder.v = 0;
    asserv_off(&deltaAsserv);
    alphaMode = M_OFF;
    alphaOrder.v = 0;
    asserv_off(&alphaAsserv);
    done();
}

void motion_block() {
    // delta
    deltaMode = M_FIX;
    deltaState.x = 0;
    deltaOrder.x = 0;
    deltaOrder.v = 0;
    asserv_set_pos_mode(&deltaAsserv);
    // alpha
    alphaMode = M_FIX;
    alphaState.x = 0;
    alphaOrder.x = 0;
    alphaOrder.v = 0;
    asserv_set_pos_mode(&alphaAsserv);
}

void motion_reach_x(float x, float v, float a) {
    float dist = (x - odo_get_x()) / cos(odo_get_theta());
    motion_dist_rot(dist, 0, v, a, vRotMaxDefault, aRotMaxDefault);
}

void motion_reach_y(float y, float v, float a) {
    float dist = (y - odo_get_y()) / sin(odo_get_theta());
    motion_dist_rot(dist, 0, v, a, vRotMaxDefault, aRotMaxDefault);
}

void motion_reach_theta(float theta, float v, float a) {
    float rot = theta - odo_get_theta();
    while (rot > PI) rot -= 2*PI;
    while (rot < -PI) rot += 2*PI;
    motion_dist_rot(0, rot, vDistMaxDefault, aDistMaxDefault, v, a);
}

void motion_set_epsilons(float Ed, float Es, float Et, float Eo) {
    epsDist = Ed;
    epsSpeed = Es;
    epsTheta = Et;
    epsOmega = Eo;
}

void motion_get_errors(float *deltaErr, float *deltaDeriv, float *deltaInte,
        float *alphaErr, float *alphaDeriv, float *alphaInte) {
    asserv_get_errors(&(deltaAsserv), deltaErr, deltaDeriv, deltaInte);
    asserv_get_errors(&(alphaAsserv), alphaErr, alphaDeriv, alphaInte);
}

void motion_get_orders(float *deltaOrder, float *alphaOrder,
        int *leftOrder, int *rightOrder) {
    *deltaOrder = cmdDelta;
    *alphaOrder = cmdAlpha;
    *leftOrder = (int)(cmdDelta - 2 * cmdAlpha);
    *rightOrder = (int)(cmdDelta + 2 * cmdAlpha);
}
