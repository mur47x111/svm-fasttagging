package analysis;

import ch.usi.dag.dislreserver.remoteanalysis.RemoteAnalysis;
import ch.usi.dag.dislreserver.shadow.ShadowObject;


public class Remote extends RemoteAnalysis {

    long addCount = 0;

    long removeCount = 0;


    public synchronized void add (final ShadowObject obj) {
        addCount++;
    }


    public synchronized void remove (final ShadowObject obj) {
        removeCount++;
    }


    @Override
    public void atExit () {
        System.out.println ("Total add " + addCount + " & remove " + removeCount);
    }


    @Override
    public void objectFree (final ShadowObject netRef) {
    }

}
