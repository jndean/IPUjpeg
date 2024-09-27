#ifndef __IPUINTERFACE__
#define __IPUINTERFACE__
#ifdef __cplusplus
extern "C" {
#endif



enum Transition_t {
    INSTANT = 0,
    FADE = 1,
    LOCAL_H_WIPE = 2,
    CIRCLE_WIPE = 3
};


typedef struct {
    unsigned currentImage;

    int transitionImage;
    Transition_t transition;
    int transitionFrame;
    int transitionLength;
} IPURequest_t;



#ifdef __cplusplus
}
#endif
#endif // __IPUINTERFACE__ //