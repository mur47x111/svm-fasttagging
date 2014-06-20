package analysis;

import ch.usi.dag.dislre.REDispatch;


public class Local {

    public static short ADD = REDispatch.registerMethod (
        "analysis.Remote.add");

    public static short RMV = REDispatch.registerMethod (
        "analysis.Remote.remove");

}
