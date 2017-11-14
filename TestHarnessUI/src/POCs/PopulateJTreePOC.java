/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package POCs;

import java.awt.BorderLayout;
import java.awt.Container;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import javax.swing.JFrame;
import javax.swing.JScrollPane;
import javax.swing.tree.DefaultMutableTreeNode;

/**
 *
 * @author Aishu
 */

class StructureBuilder {

    public static final DefaultMutableTreeNode getTreeNode(File file) throws IOException  {

        DefaultMutableTreeNode rootNode = null;
        Map<Integer, DefaultMutableTreeNode> levelNodes = new HashMap<Integer, DefaultMutableTreeNode>();
        BufferedReader reader = new BufferedReader(new FileReader(file));
        String line;

        while( (line = reader.readLine()) != null ) {

            int level = getLevel(line);
            String nodeName = getNodeName(line, level);
            DefaultMutableTreeNode node = new DefaultMutableTreeNode(nodeName);               
            levelNodes.put(level, node);
            DefaultMutableTreeNode parent = levelNodes.get(level - 1);

            if( parent != null ) {
                parent.add(node);
            }
            else {
                rootNode = node;
            }
        }    
        reader.close();
        return rootNode;
    }

    private static final int getLevel(String line) {

        int level = 0;
        for ( int i = 0; i < line.length(); i++ ) {
            char c = line.charAt(i);
            if( c == '\t') {
                level++;
            }
            else {
                break;
            }
        }
        return level;
    }

    private static final String getNodeName(String line, int level) {
        return line.substring(level);
    }      
}


public class PopulateJTreePOC {
    
    public static void main(String[] args) throws IOException {
        File textFile = new File("tests.txt");
        StructureBuilder sb = new StructureBuilder();
        DefaultMutableTreeNode myTree = StructureBuilder.getTreeNode(textFile);
        
        JFrame frame = new JFrame("Demo | Creating JTree From File.txt");
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        Container content = frame.getContentPane();
        
        
        content.add(new JScrollPane(myTree), BorderLayout.CENTER);
        frame.setSize(275, 300);
        frame.setLocationByPlatform(true);
        frame.setVisible(true);

        
    }
}
